"""Georeferencing: WGS84 <-> a local metric frame, in Unreal's axes.

The bowl is small (~200 m) and well inside one UTM zone, so no projection library
is needed. We build a local tangent frame at an origin and use a per-axis
scale, which is accurate to well under a centimetre over 200 m at this latitude.

Unreal convention: +X north, +Y east, +Z up, centimetres from the origin.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

EARTH_RADIUS_M = 6378137.0


@dataclass(frozen=True)
class Origin:
    """The survey origin: a WGS84 point that becomes Unreal (0, 0)."""

    lat: float
    lon: float

    @property
    def metres_per_deg_lat(self) -> float:
        return math.radians(1.0) * EARTH_RADIUS_M

    @property
    def metres_per_deg_lon(self) -> float:
        return math.radians(1.0) * EARTH_RADIUS_M * math.cos(math.radians(self.lat))


@dataclass(frozen=True)
class LocalPoint:
    """A point in the local metric frame, metres from the origin."""

    north_m: float
    east_m: float
    up_m: float = 0.0


def to_local(origin: Origin, lat: float, lon: float, elevation_m: float = 0.0) -> LocalPoint:
    """WGS84 degrees + elevation (metres, ellipsoidal or orthometric) -> local frame."""
    return LocalPoint(
        north_m=(lat - origin.lat) * origin.metres_per_deg_lat,
        east_m=(lon - origin.lon) * origin.metres_per_deg_lon,
        up_m=elevation_m,
    )


def to_wgs84(origin: Origin, p: LocalPoint) -> tuple[float, float]:
    """Local frame -> (lat, lon). Elevation is carried separately."""
    return (
        origin.lat + p.north_m / origin.metres_per_deg_lat,
        origin.lon + p.east_m / origin.metres_per_deg_lon,
    )


def to_unreal(p: LocalPoint, vertical_offset_m: float = 0.0) -> tuple[float, float, float]:
    """Local metres -> Unreal centimetres, relative to a chosen vertical datum.

    Unreal is left-handed with +X north, +Y east, +Z up, so north goes to X and
    east goes to Y.
    """
    return (
        p.north_m * 100.0,
        p.east_m * 100.0,
        (p.up_m - vertical_offset_m) * 100.0,
    )


def to_ue_from_wgs84(
    origin: Origin,
    lat: float,
    lon: float,
    elevation_m: float,
    vertical_offset_m: float = 0.0,
) -> tuple[float, float, float]:
    """Convenience: WGS84 straight to Unreal centimetres."""
    return to_unreal(to_local(origin, lat, lon, elevation_m), vertical_offset_m)
