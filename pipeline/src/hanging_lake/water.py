"""The lake: surface elevation, water line, and an authored depth field.

Important finding (2026-11-04): USGS 3DEP bare-earth DEM has **no bathymetry** for
Hanging Lake. Inside the lake polygon the raster is flat to within 5 cm at
2187.53 m, which is the interpolated water surface, not the bed. So:

  * the SURFACE elevation and the SHORELINE come from data and are trustworthy;
  * the BED is authored, not measured.

That distinction matters because the bed drives the aiming mechanic (how far
below a fish the arrow must be placed). The depth model here is a smooth basin
that respects the real OSM outline and reaches a configurable maximum. It is a
plausible shallow alpine pond profile, not a survey. Replace it with real
bathymetry if one is ever obtained.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from shapely.geometry import MultiPolygon, Polygon

from .dem import Dem
from .geo import Origin, to_local

# Lidar water surface, metres above sea level. The interior of the lake polygon
# sits at a flat 2187.53 m in 3DEP, which is 7,177 ft. (The Forest Service
# publishes 7,323 ft for the lake; the 146 ft gap is a datum/publication
# difference, documented in the spec. The raster figure is used here because the
# shoreline and the basin must agree with each other.)
SURFACE_ELEVATION_M = 2187.53

# Authored maximum depth. Hanging Lake is a small, shallow travertine pond; a
# couple of metres in the deepest pool is consistent with the photos and with the
# size of the bowl. NOT measured -- see module docstring.
DEFAULT_MAX_DEPTH_M = 2.2


@dataclass
class Lake:
    polygon: Polygon
    surface_m: float
    depth: np.ndarray       # metres below surface, NaN on land
    bed_m: np.ndarray       # authored bed elevation, NaN on land
    max_depth_m: float
    deepest_row: int
    deepest_col: int

    @property
    def area_m2(self) -> float:
        """True area in square metres (shapely area is in degrees squared)."""
        lat0 = self.polygon.centroid.y
        m_per_deg_lat = np.radians(1.0) * 6378137.0
        m_per_deg_lon = m_per_deg_lat * np.cos(np.radians(lat0))
        return float(self.polygon.area * m_per_deg_lat * m_per_deg_lon)


def build_lake(
    dem: Dem,
    osm_polygons: list[Polygon],
    max_depth_m: float = DEFAULT_MAX_DEPTH_M,
) -> Lake:
    """Build the water body: real outline + surface, authored depth."""
    poly = _largest(osm_polygons)
    mask = rasterise(dem, poly)

    if not mask.any():
        raise ValueError("lake polygon covers no raster pixels; check bbox and geometry")

    depth = _basin_profile(dem, poly, mask, max_depth_m)
    bed = np.where(mask, SURFACE_ELEVATION_M - depth, np.nan)

    flat = np.where(mask, depth, -np.inf)
    r, c = np.unravel_index(int(np.argmax(flat)), flat.shape)

    return Lake(
        polygon=poly,
        surface_m=SURFACE_ELEVATION_M,
        depth=depth,
        bed_m=bed,
        max_depth_m=float(np.nanmax(depth)),
        deepest_row=int(r),
        deepest_col=int(c),
    )


def _basin_profile(
    dem: Dem, poly: Polygon, mask: np.ndarray, max_depth_m: float
) -> np.ndarray:
    """A shallow basin that deepens toward the lake's interior.

    Distance-to-shore is normalised against the deepest interior cell, then
    shaped with a square root so most of the lake stays shallow and only the
    middle pool reaches full depth -- which is what a travertine pond of this
    size looks like.
    """
    from shapely import distance as shp_distance, points as shp_points

    rows, cols = dem.shape
    lons = dem.bbox.west + (np.arange(cols) + 0.5) / cols * dem.bbox.width_deg
    lats = dem.bbox.north - (np.arange(rows) + 0.5) / rows * dem.bbox.height_deg
    xx, yy = np.meshgrid(lons, lats)

    # Distance from every cell to the shoreline, in degrees. shapely's distance
    # is a vectorised ufunc, so it needs Points on the array side.
    dist_deg = shp_distance(poly.boundary, shp_points(xx, yy))
    dist_deg = np.where(mask, dist_deg, np.nan)

    # Convert to metres via the local scale, then normalise.
    m_per_deg = np.radians(1.0) * 6378137.0 * np.cos(np.radians(poly.centroid.y))
    dist_m = dist_deg * m_per_deg

    peak = np.nanmax(dist_m)
    if not np.isfinite(peak) or peak <= 0:
        raise ValueError("degenerate lake polygon: no interior distance")

    t = np.clip(dist_m / peak, 0.0, 1.0)
    return np.where(mask, max_depth_m * np.sqrt(t), np.nan)


def rasterise(dem: Dem, poly: Polygon) -> np.ndarray:
    """Point-in-polygon over the raster grid, in geographic coordinates."""
    from shapely import contains_xy

    rows, cols = dem.shape
    lons = dem.bbox.west + (np.arange(cols) + 0.5) / cols * dem.bbox.width_deg
    lats = dem.bbox.north - (np.arange(rows) + 0.5) / rows * dem.bbox.height_deg
    xx, yy = np.meshgrid(lons, lats)
    return contains_xy(poly, xx, yy)


def outline_local(lake: Lake, origin: Origin) -> list[tuple[float, float]]:
    """Water line as local-frame metres (north, east)."""
    return [
        (to_local(origin, lat, lon).north_m, to_local(origin, lat, lon).east_m)
        for lon, lat in lake.polygon.exterior.coords
    ]


def _largest(polys: list[Polygon]) -> Polygon:
    if not polys:
        raise ValueError("no lake polygon supplied")
    best = max(polys, key=lambda p: p.area)
    if isinstance(best, MultiPolygon):
        best = max(best.geoms, key=lambda p: p.area)
    return best
