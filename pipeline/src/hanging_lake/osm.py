"""OSM ingest for the bowl, via Overpass."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

import requests
from shapely.geometry import LineString, Polygon

from .dem import BBox

CACHE_DIR = Path("data/cache")

# overpass-api.de occasionally 502s on this server's egress; the kumi mirror held
# up reliably during the research pass.
ENDPOINTS = [
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass-api.de/api/interpreter",
]
USER_AGENT = "hanging-lake-ue/0.1 (Hanging Lake bowl reconstruction)"


@dataclass
class BowlFeatures:
    lake: list[Polygon] = field(default_factory=list)
    streams: list[LineString] = field(default_factory=list)
    paths: list[LineString] = field(default_factory=list)


def fetch_features(bbox: BBox | None = None, timeout: int = 90) -> BowlFeatures:
    """Fetch the lake polygon, streams and paths inside the bowl.

    Tries the live Overpass endpoints, then falls back to a cached pull on disk.
    Overpass 502s often enough that a hard dependency on it makes the build
    unreliable; the cache keeps runs reproducible.
    """
    if bbox is None:
        from .dem import DEFAULT_EAST, DEFAULT_NORTH, DEFAULT_SOUTH, DEFAULT_WEST

        bbox = BBox(DEFAULT_WEST, DEFAULT_SOUTH, DEFAULT_EAST, DEFAULT_NORTH)

    b = f"{bbox.south},{bbox.west},{bbox.north},{bbox.east}"
    query = f"""
    [out:json][timeout:{timeout}];
    (
      way["natural"="water"]({b});
      way["waterway"]({b});
      way["highway"~"^(path|footway|steps)$"]({b});
      way["man_made"="bridge"]({b});
    );
    out geom;
    """

    last: Exception | None = None
    for endpoint in ENDPOINTS:
        try:
            r = requests.post(
                endpoint,
                data={"data": query},
                headers={"User-Agent": USER_AGENT},
                timeout=timeout + 30,
            )
            r.raise_for_status()
            payload = r.json()
            _save_cache(payload, bbox)
            return _parse(payload)
        except Exception as exc:
            last = exc

    cached = _load_cache(bbox)
    if cached is not None:
        return _parse(cached)

    raise RuntimeError(f"Overpass failed on every endpoint and no cache: {last}")


def cache_path(bbox: BBox) -> Path:
    key = f"{bbox.south:.4f}_{bbox.west:.4f}_{bbox.north:.4f}_{bbox.east:.4f}"
    return CACHE_DIR / f"osm_{key}.json"


def _save_cache(payload: dict, bbox: BBox) -> None:
    try:
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        cache_path(bbox).write_text(json.dumps(payload))
    except OSError:
        pass  # caching is an optimisation, never a failure mode


def _load_cache(bbox: BBox) -> dict | None:
    p = cache_path(bbox)
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text())
    except (OSError, ValueError):
        return None


def _parse(payload: dict) -> BowlFeatures:
    f = BowlFeatures()

    for el in payload.get("elements", []):
        if el.get("type") != "way":
            continue
        geom = el.get("geometry") or []
        if len(geom) < 2:
            continue

        coords = [(pt["lon"], pt["lat"]) for pt in geom]
        tags = el.get("tags", {})

        if tags.get("natural") == "water":
            if len(coords) >= 4:
                closed = coords if coords[0] == coords[-1] else coords + [coords[0]]
                f.lake.append(Polygon(closed))
        elif tags.get("waterway"):
            f.streams.append(LineString(coords))
        elif tags.get("highway") or tags.get("man_made") == "bridge":
            f.paths.append(LineString(coords))

    return f
