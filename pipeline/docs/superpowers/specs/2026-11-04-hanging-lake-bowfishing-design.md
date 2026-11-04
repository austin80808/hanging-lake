# Hanging Lake Bowfishing — Design Specification

**Date:** 2026-11-04
**Engine:** Unreal Engine 5.8 (Apple Silicon, installed at `/Users/Shared/Epic Games/UE_5.8`)
**Repo:** `~/dev/hanging-lake-ue` (data half) + a UE project in `~/Documents/Unreal Projects/`
**Status:** Spec. No Unreal work begun — the Red Rocks project has priority on the editor.

---

## 1. What this is

A photorealistic, atmospheric bowfishing game set in the Hanging Lake bowl in
Glenwood Canyon, Colorado. No timer, no score, no fail state. A small, persistent
population of fish in a real, data-derived basin, under water so clear that
judging depth *is* the game.

The fantasy: standing on travertine in cold air, spotting a fish fifteen metres
out, and knowing — because you can read the water — exactly how far below it to
put the arrow.

### Why this place

Hanging Lake is a travertine-dammed lake at 7,177 ft, reached by a steep 1.4 mi
trail, in a bowl whose walls rise several hundred feet. It is unusually clear, the
bed is pale travertine, and a spring (Spouting Rock) issues straight out of the
cliff above it. Those three facts — clarity, pale bed, cliff spring — are what make
the refraction mechanic legible and beautiful rather than murky and frustrating.

### The fiction, stated plainly

**Fishing is prohibited at Hanging Lake, and swimming is prohibited.** The real
lake is a permitted, protected travertine preserve. This game is therefore a
fictional use of a real place. The project should not claim otherwise, and the
in-game world may omit permit signage only because it is a game.

Design consequence: this is a *reverent* take, not a "break the rules" arcade
premise. The tone is quiet observation. Nothing in the game rewards damage to the
place.

---

## 2. Player experience

### Session shape

There is no clock and no quota. A session is: enter the bowl, read the water,
find fish, take shots, fill out the journal, leave when you choose.

Progress is **knowledge**, tracked in a species journal. Each species has three
states, reached in order:

1. **Observed** — the player saw it holding, at a distance.
2. **Stalked** — got within effective bow range without spooking it.
3. **Landed** — hit and retrieved it.

Each entry records the conditions at the moment it was earned: hour of day, light
level, depth, and position in the bowl. That is the entire progression system.
Late-game it reads as a map of the lake's behaviour, and that map is the reward.

### The core mechanic: honest refraction

The reticle is not a lie. The arrow is not a hitscan.

A ray leaves the camera, intersects the water surface, refracts by Snell's law
(n_air 1.0 → n_water 1.33), and continues to the bed. The bow fires a physical
projectile along that bent path. Consequently:

- The fish you see is **not** where the fish is. Aiming at the image undershoots.
- The error scales with viewing angle. Steep-angled shots are nearly true;
  shallow-angled shots are badly deceived.
- Depth changes it too — deeper fish, larger error.

This is the real physics of spearfishing, and it is the whole difficulty curve. It
must be **taught, not hidden**: the first landed catch should visually trace the
bent path with a thin overlay line so the player sees what happened.

**Risk:** a player who does not understand the mechanic will read it as a broken
hitbox. Mitigation is that first-catch overlay plus a subtle pre-shot indicator of
the aim ray's surface entry point. It must never show the *solution* — only that
the path bends.

---

## 3. Scope

### In

- The lake bowl, ~122 × 141 m of terrain at 1 m resolution, centered on the lake.
- Hanging Lake itself: real outline, real surface elevation, authored bathymetry.
- Spouting Rock and the inlet fall as landmarks.
- The outlet stream and the shoreline the player can walk.
- A persistent fish population with light- and cover-driven behaviour.
- Bow, arrows, retrieval.
- Species journal.

### Out (deliberately)

- The 1.4 mi approach trail. The player starts at the lake.
- Interior spaces, caves, anything not visible from the bowl.
- Multiplayer, scoring, leaderboards, unlocks, currency.
- VR (revisit later; the mechanic would suit it).

---

## 4. Data foundation

Already built and tested in `~/dev/hanging-lake-ue`. 16 tests pass.

### What is measured

| Layer | Source | Confidence |
|---|---|---|
| Terrain | USGS 3DEP 1 m DEM | High — real lidar, 1 m/px |
| Lake outline | OSM/NHD polygon (17 vertices) | High — NHD-derived |
| Water surface | 3DEP flat interior = 2187.53 m (7,177 ft) | High |
| Streams, paths | OSM | High |
| **Bathymetry** | **authored** | **Low — see below** |

### The bathymetry problem

USGS 3DEP has **no bathymetry** here. Inside the lake polygon the raster is flat to
within 5 cm at 2187.53 m — that is the interpolated water *surface*, not a bed.

So the bed is authored: a smooth basin fitted to the real shoreline, deepening by a
square-root distance profile toward the interior, with a configurable maximum
(default 2.2 m, giving 1.25 m mean depth). This is plausible for a small travertine
pond and consistent with the photographs, but **it is not a survey**.

The spec treats it as a tunable, not a fact. It should be an exposed value in the
UE project so it can be changed without a rebuild. If real bathymetry is ever
obtained, it drops straight into `water.build_lake`.

### The elevation discrepancy

The Forest Service publishes 7,323 ft for Hanging Lake. 3DEP's flat interior is
7,177 ft. That is a 146 ft gap — too large to be noise, most likely a datum or
publication difference. This project uses the raster figure because the shoreline
and the basin must agree with each other; mixing datums would put the water
surface below the bed in places. Recorded here rather than silently reconciled.

### Outputs

```
data/bowl.json                    origin, datum, extents, water surface, lake outline
data/raw/bowl_heightmap.png       16-bit, 1415 × 1225, 0..65535 across the raster
data/raw/depth_m.npy              float32 depth field, NaN on land
```

`hanging_lake.export.depth_at_world(x_cm, y_cm)` answers depth at any Unreal
coordinate — the runtime needs this for fish placement and arrow impact.

---

## 5. Architecture

### Unreal project

UE 5.8, C++ project, Lumen + Nanite. **World Partition off** — the bowl is under
150 m across; one loaded level is the correct choice and the fidelity budget
depends on it.

### Modules

| Module | Responsibility | Depends on |
|---|---|---|
| `HLTerrain` | Landscape from heightmap, runtime height/depth queries | UE Landscape |
| `HLWater` | Surface shading, refraction, caustics | `HLTerrain` |
| `HLAim` | Snell solve, aim ray, projectile ballistics | `HLWater` |
| `HLFish` | Agents, state machine, population director | `HLTerrain`, `HLWater` |
| `HLJournal` | Species entries, persistence | — |
| `HLBow` | Weapon, draw, release, retrieval | `HLAim` |

Each is independently testable. `HLAim` in particular should be a pure-math module
with its own automation tests — it is the mechanic, and it must not depend on the
renderer to be correct.

### Water rendering

A translucent water volume over a real bed, not a single-layer Water-plugin body:

- IOR 1.33, per-channel absorption so depth reads blue-green with distance.
- Caustics projected onto the bed from the surface normals.
- Because refraction is physical rather than screen-space, looking straight down and
  looking across the surface show genuinely different beds. That difference is the
  visual payoff and it must survive.

**Risk:** caustics on Nanite geometry are the single most expensive effect here.
Prototype early; a light-function caustic projection may be needed instead of a
refraction pass.

### Fish

Pooled actors, one shared director. State machine per fish:

- **Hold** — hovering over structure; lowest visibility; what the journal is about.
- **Cruise** — slow patrol along the shelf.
- **Startle** — a miss or a shadow sends it to deep water; recovers over minutes.
- **Rest** — under the falls or undercut ledges.

Population is small, countable and persistent. No respawn. Missing matters because
that fish remembers.

**Risk:** a small persistent population can read as *empty* rather than *scarce*.
Prototype must prove the bowl feels inhabited at low counts before content work
starts.

---

## 6. Build order

Each phase ends in something playable or inspectable.

1. **Terrain** — landscape from heightmap, correct scale, walkable bowl.
2. **Water** — surface, refraction, bed visible. Verify looking-down vs across.
3. **Aim** — Snell solve + projectile, no fish yet. Verify against a static target
   at known depth. This is the first real test of the game.
4. **Fish** — a handful, holding and cruising, spookable.
5. **Bow** — draw, release, retrieval, arrow physics through the surface.
6. **Journal** — the three-state progression and condition capture.
7. **Dressing** — vegetation, deadfall, the bridge, the falls.

Phases 1–3 are a vertical slice. If refraction does not feel good at phase 3, stop
and rethink before building content on it.

---

## 7. Open questions

- **Bathymetry:** can a real survey be found? The bed drives the mechanic, and an
  authored 2.2 m basin may prove too shallow or too deep in play.
- **Fish species:** Hanging Lake historically held brook trout plus introduced
  rainbow and cutthroat. Whether the game uses real species or a fictional set is
  undecided.
- **Time of day:** static golden hour, or a slowly moving sun? The latter multiplies
  lighting work but makes the journal's hour field meaningful.
- **Audio:** not specified. Water, wind, birdsong and bowstring are load-bearing for
  the atmosphere and belong in the plan even though they are not visual.

---

## 8. Constraints honoured

- **No Unreal editor work while the Red Rocks project is running.** Nothing in this
  session launched UE. The data package is engine-agnostic and can sit until the
  editor is free.
- No changes to any Lowtemp system. This is a personal project.
