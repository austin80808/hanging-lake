"""CLI: build the Hanging Lake bowl package for Unreal."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from . import dem as dem_mod
from . import osm as osm_mod
from . import water as water_mod
from .geo import to_local, to_unreal


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="hanginglake")
    ap.add_argument("--out", default="data", help="output directory")
    ap.add_argument("--res", type=float, default=1.0, help="metres per pixel")
    ap.add_argument("--json", action="store_true", help="print summary as JSON")
    ap.add_argument(
        "--max-depth",
        type=float,
        default=water_mod.DEFAULT_MAX_DEPTH_M,
        help="authored maximum depth in metres (3DEP has no bathymetry)",
    )
    args = ap.parse_args(argv)

    out = Path(args.out)
    (out / "raw").mkdir(parents=True, exist_ok=True)

    origin = dem_mod.DEFAULT_ORIGIN
    print(f"origin  {origin.lat:.5f}, {origin.lon:.5f}", file=sys.stderr)

    print("fetching 3DEP lidar...", file=sys.stderr)
    raster = dem_mod.fetch(metres_per_pixel=args.res)
    print(
        f"  {raster.shape[1]}x{raster.shape[0]} px  "
        f"{raster.cell_m_x:.2f}x{raster.cell_m_y:.2f} m/px  "
        f"{raster.min_elevation:.1f}..{raster.max_elevation:.1f} m",
        file=sys.stderr,
    )

    print("fetching OSM...", file=sys.stderr)
    feats = osm_mod.fetch_features(raster.bbox)
    print(
        f"  {len(feats.lake)} water, {len(feats.streams)} streams, {len(feats.paths)} paths",
        file=sys.stderr,
    )

    lake = water_mod.build_lake(raster, feats.lake, max_depth_m=args.max_depth)
    print(
        f"lake    {lake.area_m2:,.0f} m2  max depth {lake.max_depth_m:.2f} m "
        f"(authored)  surface {lake.surface_m:.2f} m",
        file=sys.stderr,
    )

    # --- outputs -----------------------------------------------------------
    dem_mod.write_heightmap(raster, str(out / "raw" / "bowl_heightmap.png"))
    np.save(out / "raw" / "depth_m.npy", lake.depth)

    # Raw float32 heights for runtime loading by the game (no image plugin needed).
    # Row 0 is north. NaN is written as the raster minimum so the mesh stays closed.
    heights = np.where(np.isfinite(raster.data), raster.data, raster.min_elevation)
    (out / "raw" / "heights_m.f32").write_bytes(
        heights.astype("<f4").tobytes()
    )
    # Depth: NaN encoded as a negative sentinel so the runtime sees "land" clearly.
    depths = np.where(np.isfinite(lake.depth), lake.depth, -1.0)
    (out / "raw" / "depth_m.f32").write_bytes(
        depths.astype("<f4").tobytes()
    )

    bed_datum = float(np.nanmin(raster.data))
    rows, cols = raster.shape

    def world_from_lonlat(lon: float, lat: float) -> tuple[float, float]:
        x, y, _ = to_unreal(to_local(origin, lat, lon), bed_datum)
        return x, y

    def world_from_px(r: int, c: int) -> tuple[float, float]:
        lon = raster.bbox.west + (c + 0.5) / cols * raster.bbox.width_deg
        lat = raster.bbox.north - (r + 0.5) / rows * raster.bbox.height_deg
        return world_from_lonlat(lon, lat)

    surface_z_cm = round((lake.surface_m - bed_datum) * 100, 1)

    outline = []
    for lon, lat in lake.polygon.exterior.coords:
        x, y = world_from_lonlat(lon, lat)
        outline.append(
            {"x": round(x, 1), "y": round(y, 1), "z": surface_z_cm}
        )

    flat = np.where(np.isfinite(lake.depth), lake.depth, -np.inf)
    drow, dcol = np.unravel_index(int(np.argmax(flat)), flat.shape)
    dx, dy = world_from_px(int(drow), int(dcol))

    bowl = {
        "origin": {"lat": origin.lat, "lon": origin.lon},
        "vertical_datum_m": round(bed_datum, 3),
        "surface_elevation_m": lake.surface_m,
        "water_surface_z_cm": surface_z_cm,
        "grid": {"width": int(raster.shape[1]), "height": int(raster.shape[0])},
        "heightmap": "raw/bowl_heightmap.png",
        "heightmap_extent_cm": {
            "x": round(raster.bbox.height_deg * origin.metres_per_deg_lat * 100, 1),
            "y": round(raster.bbox.width_deg * origin.metres_per_deg_lon * 100, 1),
            "z": round((raster.max_elevation - raster.min_elevation) * 100, 1),
        },
        "lake": {
            "area_m2": round(lake.area_m2, 1),
            "max_depth_m": round(lake.max_depth_m, 2),
            "bathymetry_source": "authored",
            "note": (
                "USGS 3DEP has no bathymetry here; the bed is a smooth basin "
                "fitted to the real shoreline. Replace with a survey if acquired."
            ),
            "deepest_point": {
                "x": round(dx, 1),
                "y": round(dy, 1),
                "depth_m": round(float(lake.depth[drow, dcol]), 2),
            },
            "outline": outline,
        },
        "streams": len(feats.streams),
        "paths": len(feats.paths),
    }
    (out / "bowl.json").write_text(json.dumps(bowl, indent=2))

    summary = {
        "origin": bowl["origin"],
        "raster": f"{raster.shape[1]}x{raster.shape[0]}",
        "resolution_m": [round(raster.cell_m_x, 3), round(raster.cell_m_y, 3)],
        "elevation_m": [round(raster.min_elevation, 1), round(raster.max_elevation, 1)],
        "lake_area_m2": round(lake.area_m2, 1),
        "max_depth_m_authored": round(lake.max_depth_m, 2),
        "outline_points": len(outline),
        "water_surface_z_cm": surface_z_cm,
    }

    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        for k, v in summary.items():
            print(f"{k:20} {v}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
