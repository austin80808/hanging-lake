"""Tests for the Hanging Lake bowl package (no network)."""

from __future__ import annotations

import math

import numpy as np
import pytest
from shapely.geometry import Polygon

from hanging_lake import dem as dem_mod
from hanging_lake import water as water_mod
from hanging_lake.geo import Origin, to_local, to_unreal, to_wgs84

ORIGIN = Origin(lat=39.60139, lon=-107.18861)

# A small square lake ~100 m on a side, centred on the origin.
SIDE_DEG_LAT = 100.0 / ORIGIN.metres_per_deg_lat
SIDE_DEG_LON = 100.0 / ORIGIN.metres_per_deg_lon


def square_lake() -> Polygon:
    lat0, lon0 = ORIGIN.lat, ORIGIN.lon
    h = SIDE_DEG_LAT / 2
    w = SIDE_DEG_LON / 2
    return Polygon(
        [
            (lon0 - w, lat0 - h),
            (lon0 + w, lat0 - h),
            (lon0 + w, lat0 + h),
            (lon0 - w, lat0 + h),
        ]
    )


def flat_dem(elevation: float = 2187.53, pad_deg: float = 0.004) -> dem_mod.Dem:
    """A raster that is flat at the water surface, like real 3DEP under a lake."""
    n = 200
    bbox = dem_mod.BBox(
        ORIGIN.lon - pad_deg, ORIGIN.lat - pad_deg, ORIGIN.lon + pad_deg, ORIGIN.lat + pad_deg
    )
    return dem_mod.Dem(data=np.full((n, n), elevation, dtype=np.float32), bbox=bbox)


class TestGeo:
    def test_origin_maps_to_zero(self):
        p = to_local(ORIGIN, ORIGIN.lat, ORIGIN.lon)
        assert p.north_m == pytest.approx(0.0, abs=1e-9)
        assert p.east_m == pytest.approx(0.0, abs=1e-9)

    def test_one_degree_north_is_one_degree_of_meridian(self):
        p = to_local(ORIGIN, ORIGIN.lat + 1.0, ORIGIN.lon)
        expected = math.radians(1.0) * 6378137.0
        assert p.north_m == pytest.approx(expected, rel=1e-9)

    def test_longitude_scale_shrinks_with_latitude(self):
        assert ORIGIN.metres_per_deg_lon < ORIGIN.metres_per_deg_lat

    def test_roundtrip(self):
        lat, lon = 39.60210, -107.18900
        p = to_local(ORIGIN, lat, lon)
        back = to_wgs84(ORIGIN, p)
        assert back[0] == pytest.approx(lat, abs=1e-12)
        assert back[1] == pytest.approx(lon, abs=1e-12)

    def test_unreal_is_centimetres_with_x_north(self):
        p = to_local(ORIGIN, ORIGIN.lat + 0.0001, ORIGIN.lon + 0.0001, elevation_m=2187.53)
        x, y, z = to_unreal(p, vertical_offset_m=2000.0)
        assert x == pytest.approx(p.north_m * 100, rel=1e-9)   # +X is north
        assert y == pytest.approx(p.east_m * 100, rel=1e-9)    # +Y is east
        assert z == pytest.approx((2187.53 - 2000.0) * 100, abs=1e-6)

    def test_100_metres_north(self):
        lat = ORIGIN.lat + 100.0 / ORIGIN.metres_per_deg_lat
        p = to_local(ORIGIN, lat, ORIGIN.lon)
        assert p.north_m == pytest.approx(100.0, rel=1e-9)


class TestDem:
    def test_sample_centre(self):
        d = flat_dem()
        assert d.sample(ORIGIN.lat, ORIGIN.lon) == pytest.approx(2187.53, abs=0.01)

    def test_sample_outside_is_nan(self):
        d = flat_dem()
        assert math.isnan(d.sample(0.0, 0.0))

    def test_resolution_matches_raster_shape(self):
        d = flat_dem()
        # A 200 px raster over ~800 m of longitude is ~4 m/px; the point is that
        # cell size is derived from the bbox and shape rather than assumed.
        span_x = d.bbox.width_deg * ORIGIN.metres_per_deg_lon
        assert d.cell_m_x == pytest.approx(span_x / d.shape[1], rel=1e-6)
        assert d.cell_m_y > 0

    def test_heightmap_is_true_16_bit(self, tmp_path):
        d = flat_dem()
        d.data[10, 10] = 2190.0  # give it a range
        path = tmp_path / "h.png"
        dem_mod.write_heightmap(d, str(path))
        from PIL import Image

        im = Image.open(path)
        a = np.array(im)
        assert a.dtype == np.uint16
        assert a.max() == 65535
        assert a.min() == 0


class TestLake:
    def test_area_is_square_metres(self):
        lake = water_mod.build_lake(flat_dem(), [square_lake()], max_depth_m=2.0)
        # ~100 x 100 m, within raster snapping
        assert 8000 < lake.area_m2 < 12000

    def test_depth_never_exceeds_max(self):
        lake = water_mod.build_lake(flat_dem(), [square_lake()], max_depth_m=2.0)
        assert lake.max_depth_m <= 2.0 + 1e-6

    def test_deepest_point_is_interior(self):
        lake = water_mod.build_lake(flat_dem(), [square_lake()], max_depth_m=2.0)
        assert lake.max_depth_m == pytest.approx(2.0, rel=0.05)

    def test_shallow_at_the_shore(self):
        lake = water_mod.build_lake(flat_dem(), [square_lake()], max_depth_m=2.0)
        edge = np.nanmin(lake.depth[np.isfinite(lake.depth)])
        assert edge < 0.35

    def test_no_water_raises(self):
        far = Polygon([(0, 0), (1, 0), (1, 1), (0, 1)])
        with pytest.raises(ValueError):
            water_mod.build_lake(flat_dem(), [far], max_depth_m=2.0)

    def test_picks_the_largest_polygon(self):
        small = square_lake().buffer(-0.0002)
        lake = water_mod.build_lake(flat_dem(), [small, square_lake()], max_depth_m=2.0)
        assert lake.polygon is not small
        assert lake.area_m2 > 4000
