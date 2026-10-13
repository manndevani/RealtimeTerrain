# RealtimeTerrain

A runtime-editable chunk terrain and GIS framework for **Unreal Engine 5.8**. It is built for
regional and planetary geographic datasets: real-time procedural sculpting, GeoTIFF/DEM and
multi-band raster import with auto-detection, Cesium georeferencing, modular buildings, runtime 3D
model import, OpenStreetMap building import, full multiplayer replication and world persistence.

Everything works at **runtime**, in a packaged game as well as the editor. The terrain, buildings
and models are edited live through an in-game HUD and a reusable transform gizmo.

## Features

- **Two session modes.** *Sandbox* starts a procedural terrain you can sculpt immediately. *GIS
  DEM* loads real-world GeoTIFF elevation (designed for datasets of 435+ km) in large
  georeferenced chunks.
- **Runtime terrain sculpting.** Raise, lower, smooth and flatten brushes on `FDynamicMesh3` chunks
  with live collision, on separate Base / Sculpt / Detail height layers.
- **GIS raster layers.** GeoTIFF overlays are classified automatically from GDAL metadata (satellite
  and ortho imagery including 16-bit, hillshade, slope, aspect, roughness, elevation). Duplicates and
  CRS mismatches are rejected with a clear message.
- **Cesium georeferencing and navigation.** WGS84 coordinates, East-South-Up horizon leveling,
  altitude-scaled flight speed and one-key top-down framing. Local quantized-mesh terrain and XYZ
  imagery pyramids can be imported fully offline, with no Cesium ion token.
- **Modular buildings.** Spline footprints, a floor count, and terrain pads that level the ground
  under each building without permanently destroying the sculpt.
- **Runtime 3D model import.** `.obj` (+`.mtl`), `.fbx`, `.gltf`/`.glb`, `.stl` and `.ply`, with
  textures, generated thumbnails, an asset palette and a scene outliner. No editor-only SDKs are
  needed, so this works in packaged builds.
- **OpenStreetMap import.** Buildings from an `.osm` export become editable buildings, with heights
  taken from `building:levels` and `height` tags.
- **Transform gizmo.** Translate, rotate and scale handles with world/local space, snapping and
  Escape-to-cancel. Hit-testing is exact math, not physics traces, and the gizmo stays usable at any
  camera distance.
- **Undo, redo and saves.** `Ctrl+Z`/`Ctrl+Y` cover terrain strokes and building/model edits.
  Worlds save to and load from disk.
- **Multiplayer.** Server-authoritative editing, with sculpt strokes broadcast as small parameter
  sets rather than height data. Buildings, models and raster layers replicate.

Each feature is documented in detail in [docs/FEATURES.md](docs/FEATURES.md).

## Requirements

- **Windows 64-bit.** The plugin modules and the GDAL setup only target Win64.
- **Unreal Engine 5.8**, installed through the Epic Games Launcher.
- **[Cesium for Unreal](https://cesium.com/platform/cesium-for-unreal/)**, installed to engine 5.8
  from Fab (free).
- **Visual Studio 2022** with the *Game development with C++* workload, as required by UE 5.8.
- **Git LFS.** All `.uasset`/`.umap` files are stored in LFS.

The other plugins the project uses ship with the engine: Geometry Scripting, Virtual Heightfield
Mesh, Procedural Mesh Component, Celestial Vault, Day Sequence, PCG and GeoReferencing.

### Third-party setup: GDAL

GeoTIFF/DEM import links against [GDAL](https://gdal.org/), which is not bundled with Unreal. A
script builds it from source with vcpkg into `Tools/vcpkg/` (git-ignored). **The project will not
compile until this has run once.** The first run takes 30–60+ minutes and a few GB of disk.

```powershell
./Tools/Setup-GDAL.ps1
```

### Content not included in this repository

Some art used during development comes from third-party marketplace packs that cannot be
redistributed, so it is not in the repo:

| Content | Used for |
| :--- | :--- |
| `Content/SD_Art/` | Default modular building kit and holographic placement material |
| `Content/Gizmo/Materials/`, two `Content/Gizmo/Meshes/SM_Screen*` meshes, `Content/Mytaverse/` | Gizmo handle materials and the screen-space handles (the gizmo falls back to flat colors and basic shapes without them) |

The plugin's own content (startup map, terrain materials, brushes) **is** included. Replacing the
third-party dependencies with plugin-owned assets is on the roadmap.

## Getting started

```powershell
git lfs install
git clone https://github.com/manndevani/RealtimeTerrain.git
cd RealtimeTerrain
./Tools/Setup-GDAL.ps1
```

Then either open `RealtimeTerrain.uproject` and let the editor build the modules, or build from
the command line (adjust the paths to your install and clone location):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" RealtimeTerrainEditor Win64 Development -Project="C:\path\to\RealtimeTerrain\RealtimeTerrain.uproject" -WaitMutex
```

The editor starts in `/GRT/Maps/NewWorld`. Press Play and choose **Sandbox** or **Import DEM Data**.

### Running the tests

The plugin has 94 automation tests under `GRT.*`. To run them headless:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\path\to\RealtimeTerrain\RealtimeTerrain.uproject" -ExecCmds="Automation RunTests GRT;Quit" -unattended -nopause -nosplash -nullrhi -log
```

Results are written to `Saved/Logs/RealtimeTerrain.log` (search for `Test Completed`). In the
editor, use **Tools → Test Automation** and filter for `GRT`.

## Controls

| Action | Control |
| :--- | :--- |
| Orbit / look | Right mouse button + drag |
| Fly | `W` `A` `S` `D`, `E` up, `Q` down |
| Flight speed | Mouse wheel |
| Frame whole map (top view) | `F` or `Home` |
| Sculpt terrain | Left mouse button + drag |
| Place building | Left click on terrain in Building mode |
| Place 3D model | `[Place]` in the asset palette, then left click on terrain |
| Select model / building | Left click in the viewport or the Scene Outliner |
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` |
| Gizmo: translate / rotate / scale | `Shift+W` / `Shift+E` / `Shift+R` |
| Gizmo: world / local space | `C` |
| Gizmo: snapping on/off | `G` (hold `Ctrl` while dragging to invert) |
| Cancel gizmo drag | `Escape` (restores the exact pre-drag transform) |
| Focus / hide / delete object | `🎯` / `👁️` / `🗑️` in the Scene Outliner |

## Project layout

```text
Source/RealtimeTerrain/     Game module: edit game mode and player controller
Plugins/GRT/                The framework itself (GRT runtime + GRTEditor modules)
  Source/GRT/
    Building/  Core/  Gizmo/  IO/  Physics/  Settings/  UI/  World/
    Private/Tests/          GRT.* automation tests
  Source/ThirdParty/GDAL/   GDAL link module (see "Third-party setup: GDAL")
Tools/Setup-GDAL.ps1        Builds GDAL via vcpkg
docs/                       Detailed documentation
```

## Documentation

- [docs/FEATURES.md](docs/FEATURES.md): architecture and a detailed reference for every feature.
- [docs/DEVELOPMENT_HISTORY.md](docs/DEVELOPMENT_HISTORY.md): how each system was built, bugs found
  and fixed, review and audit results, and known limitations.
- [Source/RealtimeTerrain/CODE_REFERENCE.md](Source/RealtimeTerrain/CODE_REFERENCE.md) and
  [Plugins/GRT/CODE_REFERENCE.md](Plugins/GRT/CODE_REFERENCE.md): per-file code reference.

## License and credits

RealtimeTerrain is released under the [MIT License](LICENSE).

- The `GRT` plugin began as the TerraDyne plugin by **Andras Gregori**, also MIT-licensed; see
  [Plugins/GRT/LICENSE](Plugins/GRT/LICENSE).
- Gizmo handle meshes come from
  [UnrealTransformGizmo](https://github.com/robinwood3d/UnrealTransformGizmo) by Robin Wood (MIT);
  see [Content/THIRD_PARTY_NOTICES.md](Content/THIRD_PARTY_NOTICES.md).
- Model import uses the vendored single-header libraries [ufbx](https://github.com/ufbx/ufbx) and
  [cgltf](https://github.com/jkuhlmann/cgltf) (ufbx: MIT or public domain; cgltf: MIT).
- GeoTIFF/DEM import uses [GDAL](https://gdal.org/) (MIT/X style license), built locally rather
  than distributed.
