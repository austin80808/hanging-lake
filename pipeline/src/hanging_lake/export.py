"""Hanging Lake bowl: export helpers for Unreal."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np


def bowl_summary(path: str | Path = "data/bowl.json") -> dict:
    return json.loads(Path(path).read_text())


def depth_at_world(x_cm: float, y_cm: float, data_dir: str | Path = "data") -> float:
    """Water depth at an Unreal XY position, in metres. 0.0 on land."""
    d = Path(data_dir)
    bowl = bowl_summary(d / "bowl.json")
    depth = np.load(d / "raw" / "depth_m.npy")

    extent = bowl["heightmap_extent_cm"]
    rows, cols = depth.shape

    # Unreal X is north (raster rows top-down), Y is east (raster columns).
    col = (x_cm / extent["y"]) * cols
    # Raster row 0 is north = max Y; Unreal Y increases north in our geo module
    # mapping, so flip to match the heightmap's top-down convention.
    row = rows - 1 - (y_cm / extent["x"]) * rows

    r, c = int(round(row)), int(round(col))
    if not (0 <= r < rows and 0 <= c < cols):
        return 0.0
    v = depth[r, c]
    return 0.0 if not np.isfinite(v) else float(v)


def depth_profile_along(x_cm: float, data_dir: str | Path = "data") -> list[tuple[float, float]]:
    """Depth samples along a north-south transect at a fixed Unreal X."""
    d = Path(data_dir)
    bowl = bowl_summary(d / "bowl.json")
    depth = np.load(d / "raw" / "depth_m.npy")
    extent = bowl["heightmap_extent_cm"]
    rows, cols = depth.shape

    col = int(round((x_cm / extent["y"]) * cols))
    if not (0 <= col < cols):
        return []

    out = []
    for r in range(rows):
        v = depth[r, col]
        if np.isfinite(v):
            y_cm = extent["x"] * (1.0 - (r + 0.5) / rows)
            out.append((round(y_cm, 1), round(float(v), 3)))
    return out
