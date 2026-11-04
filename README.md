# 🎣 Hanging Lake

A first-person bowfishing game set at Hanging Lake in Glenwood Canyon, Colorado. The
terrain comes from real survey data. You stand on the boardwalk, look across the
famous fallen log, and try to land trout in water that bends light: the fish is
never quite where it looks.

![Opening view from the boardwalk, panning across the log](docs/media/opening-pan.gif)

![Drawing, releasing and landing a fish](docs/media/landing-a-fish.gif)

## 🕹️ How to play

You start on the east boardwalk, looking west across the log toward the north rim
where the falls come in. Fish cruise the lake; the count is top left.

1. Aim at a fish. The reticle is the truth: aim where the fish *looks*, and the
   arrow bends at the surface the way light does.
2. Hold to draw (the bar fills), release to shoot. A full draw flies flattest.
3. A hit lands the fish and shows its length and depth. A splash near a fish spooks
   it after a moment, so take your time between shots.
4. Arrows stop at the lake bed, the rock, and the log.

| Action | Keyboard and mouse | Controller |
|---|---|---|
| Look | Mouse | Right stick (slower while drawing, for fine aim) |
| Walk | W A S D | Left stick |
| Draw, then release to shoot | Hold left mouse button | Hold right trigger |
| Jump | Space | Bottom face button (A / ✕) |
| Quit | Esc | Menu / Options button |

### 🎮 Controllers and remapping

Any controller macOS (or Windows) recognises as a gamepad works: Xbox, PlayStation,
Switch Pro, 8BitDo and other MFi or XInput pads. Every control is a named action or
axis in [`game/Config/DefaultInput.ini`](game/Config/DefaultInput.ini), so any key,
button, trigger or stick can be bound to anything:

```ini
+ActionMappings=(ActionName="Draw",Key=Gamepad_RightTrigger)
+ActionMappings=(ActionName="Draw",Key=Gamepad_LeftShoulder)   ; add a second binding
+AxisMappings=(AxisName="LookUpRate",Scale=1.0,Key=Gamepad_RightY) ; invert look
```

Actions: `Draw`, `Jump`, `Quit`. Axes: `MoveForward`, `MoveRight`, `Turn`, `LookUp`
(mouse), `TurnRate`, `LookUpRate` (sticks). Edit `DefaultInput.ini` for everyone, or
the per-player copy Unreal writes under `game/Saved/Config/<Platform>/Input.ini`.
Unusual devices (flight sticks, wheels, arcade sticks) can be mapped to gamepad
inputs with Steam Input or a tool like Joystick Mapper.

### 📱 Phones

Not yet. The game runs on desktop Unreal (macOS on Apple Silicon is what it is
tested on). Two routes are open: Unreal's Pixel Streaming, where the game runs on a
computer and streams to a phone's browser with touch controls, or a native iOS or
Android build, which needs a mobile renderer pass and touch controls.

## 🛠️ Run it

Needs **Unreal Engine 5.8** and Git LFS.

```bash
git lfs install
git clone https://github.com/austin80808/hanging-lake.git
cd hanging-lake/game
# Open HangingLake.uproject in Unreal Editor (it builds the C++ module) and press Play,
# or from a terminal on macOS:
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
  HangingLakeEditor Mac Development -Project="$PWD/HangingLake.uproject"
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$PWD/HangingLake.uproject" -game -windowed -ResX=1280 -ResY=720
```

The game reads its terrain from `pipeline/data` beside it (pass `-hldata=<dir>` to
point elsewhere). The first launch compiles shaders and can take a few minutes.

## 🗺️ What is real

| Feature | Source |
|---|---|
| Canyon terrain, 1 m grid | USGS 3DEP lidar elevation |
| Lake outline and surface height | OpenStreetMap way 40199283, 3DEP |
| Lake bed (2.2 m max) | **Authored**: 3DEP has no bathymetry here; a smooth basin fitted to the shoreline |
| Boardwalk and trail | OpenStreetMap, *Hanging Lake Trail* (ways 159737338, 159737337) |
| Falls inflow | OpenStreetMap, East Fork Dead Horse Creek |
| The fallen log | Measured off the USGS NAIP aerial photo: 14.2 m, from the north-east rim (39.601460 N, 107.191661 W) south-west to 39.601373 N, 107.191781 W |

![NAIP aerial with the OSM lake outline and boardwalk; the log is the pale diagonal](docs/media/naip-log-survey.png)

![The log from the boardwalk](docs/media/the-log.png)

Not modelled yet: the falling water itself, trees, textured rock and travertine, and
the log's exact shape in close-up (its position, length and angle are surveyed; the
taper, bow and branch stubs are authored).

## 📁 Layout

| Folder | What |
|---|---|
| [`game/`](game) | The Unreal 5.8 C++ project. Gameplay in `Source/HangingLake/`: terrain and lake bed (`HLTerrainData`), water (`HLWaterSurface`), the bow and refraction (`HLBow`, `HLSnellSolver`), fish (`HLFish`), landmarks, lighting and spawn (`HLGameMode`) |
| [`pipeline/`](pipeline) | Python that builds the terrain data from public sources. See [its README](pipeline/README.md) |
| [`docs/`](docs) | The media above. The design spec is in [`pipeline/docs/`](pipeline/docs/superpowers/specs) |

### Developer switches

Command-line flags for testing (they do nothing unless passed):

| Flag | Does |
|---|---|
| `-hlshot` | Screenshot at 45 s (after shaders finish) |
| `-hlpitch=<deg> -hlyaw=<deg> [-hlpan=<deg/s>]` | Lock (or slowly pan) the view so captures repeat |
| `-hlautoshoot [-hlshootat=<s>]` | Fire four aimed test shots at fish and log hits |
| `-hlrec=<s> -hlrecdur=<s> -hlrecfps=<n>` | Save frames for a GIF |
| `-hltrace` | Log the camera every 5 s |
| `-hlnoterrain -hlnowater -hlnofish -hlnomat` | Turn pieces off to isolate a problem |
| `-hlev=<bias>` | Exposure bias (default -1) |

## 📜 Data credits

- Elevation: U.S. Geological Survey, 3D Elevation Program (public domain).
- Aerial imagery used to place the log: USDA NAIP via the USGS National Map (public domain).
- Lake outline, trail, boardwalk and streams: © OpenStreetMap contributors, available
  under the [Open Database License](https://www.openstreetmap.org/copyright). The
  cached extracts are in `pipeline/data/cache/`.

Hanging Lake is a protected site run by the U.S. Forest Service and the City of
Glenwood Springs. Fishing and touching the water are not allowed there in real life,
and nobody may walk on the log. This game is the only way to do it.
