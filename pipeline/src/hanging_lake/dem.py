"""USGS 3DEP bare-earth DEM fetch at 1 m, clipped to the Hanging Lake bowl.

3DEP's ImageServer will resample to a requested output size, so we ask for one
pixel per metre over a bbox sized to the bowl rather than taking the native
resolution and resampling ourselves.
"""

from __future__ import annotations

import io
import time
from dataclasses import dataclass

import numpy as np
import requests
from PIL import Image

from .geo import Origin

SERVICE = "https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage"
NODATA = -9999.0
USER_AGENT = "hanging-lake-ue/0.1 (Hanging Lake bowl reconstruction)"

# The bowl: lake, both falls, the outlet stream, and the shoreline the player walks.
# Margins are generous so nothing playable sits on the raster edge. The bbox is
# widened to the west and north of the lake because Spouting Rock and the outlet
# stream (which are part of the playable bowl) lie in that direction.
DEFAULT_SOUTH = 39.5955
DEFAULT_NORTH = 39.6065
DEFAULT_WEST = -107.1985
DEFAULT_EAST = -107.1820

# Survey origin: the lake centroid from the OSM polygon, so Unreal (0, 0) sits in
# the middle of the water rather than at a guessed point. Becomes Unreal (0, 0).
DEFAULT_ORIGIN = Origin(lat=39.601387, lon=-107.191776)


@dataclass(frozen=True)
class BBox:
    west: float
    south: float
    east: float
    north: float

    @property
    def width_deg(self) -> float:
        return self.east - self.west

    @property
    def height_deg(self) -> float:
        return self.north - self.south

    def as_query(self) -> str:
        return f"{self.west},{self.south},{self.east},{self.north}"


@dataclass
class Dem:
    """Elevation raster in metres. Row 0 is the north edge, column 0 the west."""

    data: np.ndarray
    bbox: BBox

    @property
    def shape(self) -> tuple[int, int]:
        return self.data.shape  # (rows, cols)

    @property
    def cell_m_x(self) -> float:
        """East-west metres per pixel."""
        o = DEFAULT_ORIGIN
        return self.bbox.width_deg * o.metres_per_deg_lon / self.shape[1]

    @property
    def cell_m_y(self) -> float:
        """North-south metres per pixel."""
        o = DEFAULT_ORIGIN
        return self.bbox.height_deg * o.metres_per_deg_lat / self.shape[0]

    @property
    def min_elevation(self) -> float:
        return float(np.nanmin(self.data))

    @property
    def max_elevation(self) -> float:
        return float(np.nanmax(self.data))

    def sample(self, lat: float, lon: float) -> float:
        """Bilinear sample at a WGS84 point. Returns NaN outside the raster."""
        rows, cols = self.shape
        fx = (lon - self.bbox.west) / self.bbox.width_deg * (cols - 1)
        fy = (self.bbox.north - lat) / self.bbox.height_deg * (rows - 1)
        if not (0.0 <= fx <= cols - 1 and 0.0 <= fy <= rows - 1):
            return float("nan")

        x0, y0 = int(np.floor(fx)), int(np.floor(fy))
        x1, y1 = min(x0 + 1, cols - 1), min(y0 + 1, rows - 1)
        tx, ty = fx - x0, fy - y0

        d = self.data
        top = d[y0, x0] * (1 - tx) + d[y0, x1] * tx
        bottom = d[y1, x0] * (1 - tx) + d[y1, x1] * tx
        return float(top * (1 - ty) + bottom * ty)


def fetch(
    bbox: BBox | None = None,
    metres_per_pixel: float = 1.0,
    timeout: int = 120,
    retries: int = 3,
) -> Dem:
    """Fetch a 1 m float raster of the bowl from USGS 3DEP."""
    if bbox is None:
        bbox = BBox(DEFAULT_WEST, DEFAULT_SOUTH, DEFAULT_EAST, DEFAULT_NORTH)

    origin = DEFAULT_ORIGIN
    width_m = bbox.width_deg * origin.metres_per_deg_lon
    height_m = bbox.height_deg * origin.metres_per_deg_lat
    cols = max(1, int(round(width_m / metres_per_pixel)))
    rows = max(1, int(round(height_m / metres_per_pixel)))

    params = {
        "bbox": bbox.as_query(),
        "bboxSR": "4326",
        "imageSR": "4326",
        "size": f"{cols},{rows}",
        "format": "tiff",
        "pixelType": "F32",
        "noData": NODATA,
        "interpolation": "RSP_BilinearInterpolation",
        "f": "json",
    }

    last: Exception | None = None
    for attempt in range(retries):
        try:
            r = requests.get(
                SERVICE, params=params, headers={"User-Agent": USER_AGENT}, timeout=timeout
            )
            r.raise_for_status()
            meta = r.json()
            if "error" in meta:
                raise RuntimeError(f"3DEP error: {meta['error']}")

            img = requests.get(
                meta["href"], headers={"User-Agent": USER_AGENT}, timeout=timeout
            )
            img.raise_for_status()

            arr = np.array(Image.open(io.BytesIO(img.content)), dtype=np.float32)
            if arr.ndim != 2:
                arr = arr[..., 0]
            arr = np.where(arr <= NODATA + 1, np.nan, arr).astype(np.float32)
            return Dem(data=arr, bbox=bbox)
        except Exception as exc:  # network is flaky; retry with backoff
            last = exc
            time.sleep(2 * (attempt + 1))

    raise RuntimeError(f"3DEP fetch failed after {retries} attempts: {last}")


def write_heightmap(dem: Dem, path: str) -> np.ndarray:
    """Write a 16-bit PNG heightmap and return the normalised array.

    Unreal landscapes want 0..65535 across the raster's own min..max, with row 0
    at the north edge (which is Unreal's +X side).
    """
    d = dem.data.copy()
    lo, hi = nan_min_max(d)
    if hi <= lo:
        raise ValueError("flat or empty raster")

    norm = (d - lo) / (hi - lo)
    norm = np.nan_to_num(norm, nan=0.0)
    png = (norm * 65535.0).round().astype(np.uint16)
    # PIL's 16-bit greyscale is "I;16", but fromarray infers I for a uint16 array;
    # build the image explicitly so the PNG really is 16-bit.
    img = Image.new("I;16", (png.shape[1], png.shape[0]))
    img.putdata(png.ravel().tolist())
    img.save(path)
    return norm


def nan_min_max(d: np.ndarray) -> tuple[float, float]:
    finite = np.isfinite(d)
    if not finite.any():
        return 0.0, 0.0
    return float(np.min(d[finite])), float(np.max(d[finite]))
