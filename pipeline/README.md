# Hanging Lake: terrain pipeline

Builds the **Hanging Lake bowl** (Glenwood Canyon, Colorado) for Unreal Engine 5.8
from public geodata: a 1 m USGS 3DEP terrain, the real OSM/NHD lake outline, and a
depth field — all georeferenced to a single origin at the lake centroid.

The data half of a photorealistic bowfishing game. Design:
[`docs/superpowers/specs/2026-11-04-hanging-lake-bowfishing-design.md`](docs/superpowers/specs/2026-11-04-hanging-lake-bowfishing-design.md).

## Status

Working: 3DEP 1 m fetch, OSM ingest, georeferencing, lake outline and surface,
bathymetry, 16-bit heightmap export, Unreal-space queries. 16 tests pass.

The game that uses this data is in [`../game`](../game); see the
[top-level README](../README.md).

## The bathymetry caveat

USGS 3DEP has **no bathymetry** for Hanging Lake — the interior of the lake polygon
is flat at the water surface. The shoreline and surface elevation are measured; the
bed is **authored** (a smooth basin, 2.2 m max by default). It is a tunable, not a
survey. See spec §4.

## Run

```bash
python3 -m venv .venv && .venv/bin/pip install -e .
.venv/bin/hanginglake --out data
.venv/bin/python -m pytest tests/ -q
```

Outputs:

```
data/bowl.json                 origin, datum, extents, water surface, lake outline
data/raw/bowl_heightmap.png    16-bit 1415x1225, for a UE Landscape
data/raw/depth_m.npy           depth field in metres, NaN on land
data/raw/heights_m.f32         raw float32 heights (row 0 = north), read by the game
data/raw/depth_m.f32           raw float32 depths, -1 on land, read by the game
```

Overpass 502s often; `hanging_lake.osm` caches each pull under `data/cache/` and
falls back to it.

## Use from Unreal

```python
from hanging_lake.export import depth_at_world
depth_at_world(0, 0)   # metres of water at Unreal (0, 0) — 2.2 at the deepest pool
```

Unreal axes: **+X north, +Y east, +Z up, centimetres**; origin at the lake centroid.

## Origin

`39.601387, -107.191776` — the OSM lake centroid. Unreal (0, 0) is the middle of
the water.
