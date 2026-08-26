# RealtimeTerrain (Unreal Engine 5.8)

## 📋 Project Status

**Goal**: Port RoadBuilder plugin's boundary/lane/junction/elevation engineering into the runtime
road system (`ARealtimeRoadSplineActor`/`ARealtimeRoadJunctionActor`), keeping it replicated and
usable in-game. Full plan + phase breakdown: `C:\Users\Admin\.claude\plans\adaptive-noodling-penguin.md`.
Detailed history of how each phase was built is in git log (`road-system-rewrite` branch) —
this section tracks current state only, not a running journal.

### Phase 1 — COMPLETE ✅ (boundary/lane graph, style presets, replication, actor wiring)
- `RoadCurveGraph.h`/`.cpp`: `URealtimeRoadCurve`/`URealtimeRoadLane`/`URealtimeRoadBoundary`
  (cubic-Hermite offset math, Cut/Join) + `SerializeGraph`/`DeserializeGraph` replication.
- `RoadStylePresets.h`/`.cpp`: `URoadLaneMarkStyle`/`URoadLaneShape`/`URoadStylePreset` (`UDataAsset`).
- `RoadMeshBuilder::BuildRoadSectionFromStyle`: composite section from a style's lane list.
- `ARealtimeRoadSplineActor::StylePreset`: optional, replicated; drives `GenerateRoadMesh` when set,
  fully backward-compatible when unset. `CrossSectionSettings.RoadWidth` still independently
  drives terrain-pad/junction width even with a style set — no auto-derivation yet.
- Tests: 8 across `RoadCurveGraph`/`RoadStylePresets`/`RoadMeshBuilder`/`RoadSplineActor`.

### Phase 2 — COMPLETE ✅ (junction lane routing + gore markings)
- `RoadNetworkSolver`: `IsRampConnection` (shallow angle + lane-count mismatch = ramp, classified
  on both `SolveNode` corner paths — the bevel path was initially missed, since generous
  curb+footpath width is exactly what triggers it for a realistic ramp), `RampCornerRadiusMultiplier`
  (wider fillet at ramp corners), `FindGoreApex` (driving-surface convergence point),
  `MapLaneConnections` + `BuildLaneConnectorCentreline` (per-lane connectivity across a corner).
- `RoadMesh::AppendGoreFill`: raised gore-island triangle fan, self-correcting winding (verified
  against real `FDynamicMesh3::GetTriNormal`, not just "doesn't crash").
- `ARealtimeRoadJunctionActor`: wires all of the above into `RebuildJunction`'s mesh —
  `SolvedIsRampCorner`/`SolvedGoreApex`/`SolvedConnectors`, `GetRampCornerCount()`/`GetConnectorCount()`.
- Verified with a live 3-actor integration test (2-lane through-road + 1-lane ramp at 20°, the real
  highway on/off-ramp shape): exactly 1 ramp corner, exactly 4 per-lane connectors, hand-derived
  and matched exactly.
- Deferred, not blocking: `URealtimeRoadBoundary`/`URealtimeRoadLane` (Phase 1's graph classes)
  aren't consumed here — connector geometry comes from arm-level driving-width/lane-count data
  instead, which was sufficient for correct, tested connectivity. Wiring the graph in for
  asymmetric/tapered lanes is a real future enhancement, not required for what Phase 2 set out to do.
- Tests: 9 in `RoadNetworkSolver`'s suite + 1 live integration test. **Full regression sweep across
  every touched suite at Phase 2 completion: 25 tests, 0 regressions.**

### Phase 3 — COMPLETE ✅ (elevation profile + automatic flyover + pier instancing)
- `RoadElevationProfile.h`/`.cpp`: `FRoadHeightKey` + `EvaluateHeight` — cubic-Hermite height-vs-
  distance profile, ported from RoadBuilder's `FHeightPoint`/`FHeightSegment` (`RoadActor.h`).
  Reuses `RoadCurveGraph::FindElementIndex`'s binary search rather than duplicating it. Test:
  `TerraDyne.RoadElevationProfile.HeightMath` (flat, S-curve climb, 3-key climb-hold-descend).
- `RoadElevationProfile::DetectFlyoverBridges`: scans a terrain height sequence for stretches
  whose local grade exceeds a threshold for a long-enough run and returns zero-slope bridging key
  pairs (a comfortable departure/return grade). New behavior, not a RoadBuilder port — RoadBuilder's
  own height keys are always hand-authored. Test: `TerraDyne.RoadElevationProfile.AutoFlyoverDetection`
  (sawtooth stretch correctly bridged at its exact boundaries; flat sequence produces no bridge;
  a too-short steep step is gated out by the minimum-run-length check).
- `ARealtimeRoadSplineActor`: `HeightKeys` (replicated, optional, empty by default — a road with
  keys stops following terrain over that stretch), `bAutoFlyover`/`MaxComfortableGradeDegrees`/
  `MinFlyoverRunLength`, and `ApplyElevationProfile(Points, Distances)` — merges auto-detected
  bridges with any authored keys (an authored key anywhere inside an auto bridge's span suppresses
  that whole bridge pair, full stop) and overwrites `Points[i].Z` in place. Wired into
  `BuildConformedCentreline` right after terrain conforming/smoothing.
  `FRoadHeightKey`'s `USTRUCT` needed the `BlueprintType` specifier (not just `USTRUCT()`) since
  `HeightKeys` is `BlueprintReadWrite` — UHT rejects a `BlueprintReadWrite` `TArray` of a non-
  `BlueprintType` struct at compile time.
  Test: `TerraDyne.RoadSplineActor.ApplyElevationProfile` (sawtooth auto-bridges to 454.40 at
  station 10; two bracketing user keys override the same stretch to exactly 5000.0).
- `RoadPierPlacement.h`/`.cpp`: `RoadPierPlacement::PlacePiers` — pure placement math (no HISM/mesh
  dependency, same testable-in-isolation pattern as `DetectFlyoverBridges`), walking a run's
  stations at `SpacingAlongRoad` intervals and placing a pier wherever the final road height clears
  `MinGap` above the raw terrain sample. New behavior; RoadBuilder has no pier geometry at all —
  its "Elevated" style is visual only.
- `FRoadPierSettings` (`RoadTypes.h`, mirrors `FRoadScatterSettings`'s shape) + a dedicated
  `PierHISMComponent` on `ARealtimeRoadSplineActor` (a separate component from `ScatterHISMComponent`
  since piers and rough-road debris need different meshes/scaling, not one shared instance buffer).
  `GeneratePiers` builds per-station distance/XY/road-underside-height/ground-height arrays straight
  from the already-computed `RunFrames` (reusing `SampleTerrainHeightAtXY`, the same call
  `GenerateScatter` already makes) and feeds them to `PlacePiers` — no extra data needed through
  `BuildConformedCentreline`. Piers regenerate only on the settle pass, same as scatter and the
  terrain pad.
  Test: `TerraDyne.RoadPierPlacement.ElevatedStretchOnly` (piers land only within a synthetic
  500–1500cm elevated stretch, each spanning the full 500cm deck height; a flat road gets none).
- **Full regression sweep after this increment: 30 tests, 0 regressions.**
### Phase 4 — NOT STARTED (spiral/clothoid centreline smoothing)
### Phase 5 — NOT STARTED (OpenDrive/.xodr export)

**Next step**: Phase 4 — port `Spiral.h`'s Euler-spiral math for settle-pass centreline smoothing
(live-drag preview keeps the cheap polyline). Then Phase 5 (OpenDrive/.xodr export). See plan file
for full detail.


A high-performance, runtime-editable chunk terrain and GIS framework for Unreal Engine 5.8. Built for planetary/regional geographic datasets, real-time procedural sculpting, dynamic multi-band GIS/DEM raster ingestion with auto-detection, spline road networks, modular buildings, and world persistence.

---

## ⚡ Core Architecture

```text
       ┌─────────────────────────────────────────────────────────┐
       │                Session Mode Selection                   │
       └───────────────┬─────────────────────────┬───────────────┘
                       │                         │
               [SANDBOX MODE]            [GIS DEM MODE]
                       │                         │
            Procedural Fractal Grid         Dynamic GIS Layer Stack
            (Base / Sculpt / Detail)       (Master DEM + Auto-Detected Overlays)
                       │                         │
                       │             Cesium GlobeAnchor Georeferencing
                       │             Adaptive Macro Chunk Sizing (~13.7km)
                       │             16-bit Multi-Band Satellite Ingestion
                       │             Dynamic Overlay Stack & Modal Validation
                       │                         │
                       └───────────┬─────────────┘
                                   ▼
       ┌─────────────────────────────────────────────────────────┐
       │                 Runtime Chunk Framework                 │
       ├─────────────────────────────────────────────────────────┤
       │ • High-Speed FDynamicMesh3 & Dynamic Collisions         │
       │ • Cesium DynamicPawn (WGS84, ESU Horizon, Spd, TopView) │
       │ • Procedural Spline Roads & Junction Networks           │
       │ • Modular Spline Buildings & Dynamic-Step Pad Leveling  │
       │ • Full Undo/Redo (Ctrl+Z/Y) & Disk Persistence          │
       │ • Dynamic Slate HUD (Auto-Detect, Load/Unload, Delete)  │
       └─────────────────────────────────────────────────────────┘
```

---

## ✨ Features & Capabilities

### 1. Dual Independent Session Modes
* **Sandbox Mode**: Instant procedural grid initialization ready for immediate brush sculpting, layer editing, road drawing, and building placement.
* **GIS DEM Mode**: Dedicated geospatial workflow designed for massive real-world GeoTIFF datasets ($435+\text{ km}$). Bypasses procedural noise, allocates exact elevation samples, and loads macro-scale terrain tiles seamlessly.

### 2. Cesium DynamicPawn Camera Navigation
* Built on Cesium's `AGlobeAwareDefaultPawn` with native `UCesiumGlobeAnchorComponent`:
  * **WGS84 Georeferencing**: Live Longitude, Latitude, and Height coordinate tracking.
  * **ESU Horizon Leveling**: Computes true East-South-Up orientation, eliminating horizon skew and gimbal lock across planetary distances.
  * **Ellipsoid-Aligned Flight**: `MoveUp_World` automatically follows the true geodetic surface normal.
  * **Top-Down Map Framing (`F` / `Home` / HUD Button)**: Instantly computes active dataset bounds and frames the entire terrain footprint.
  * **Dynamic Flight Speed**: Automatically scales maximum flying speed ($\text{Altitude} \times 2.5$) for effortless navigation from centimeters to regional scales — scaled from the pawn's real WGS84 height, not raw Unreal Z, so it stays correct far from the `CesiumGeoreference` origin (e.g. after flying to a Tiled Cesium Import footprint on another part of the globe), where Unreal Z alone no longer means "altitude".
  * **Mouse Wheel Speed Control**: Scroll adjusts a flight-speed multiplier ($0.1\times$–$10\times$, exponential per notch) on top of the automatic altitude scaling above, instead of zooming/dollying the camera.
  * **Sculpt Isolation**: Left-click drag is dedicated to terrain editing and spline drawing; right-click drag orbits freely without fighting camera controls.
  * **World Bounds Checks disabled at startup**: `AWorldSettings::bEnableWorldBoundsChecks` is forced off in `UTerraDyneSubsystem::Initialize` (before any actor's `BeginPlay`). Left on — Unreal's default — it's incompatible with Cesium's origin rebasing: a rebase shifts every actor's absolute Z, so an airborne pawn can spuriously read as having fallen below `KillZ`, get destroyed, and respawn at the default `PlayerStart` — visible as the camera snapping back mid-flight to wherever it started.

### 3. Dynamic GIS Raster Layer Stack & Auto-Detection Engine
* **Slot 1 (Master DEM)**: Fixed reference grid establishing georeferenced bounds and base 3D terrain geometry.
* **Slots 2+ (Dynamic Overlays)**: Single `+ Add Overlay Layer…` button that launches the native file picker for any GeoTIFF raster (`.tif`, `.tiff`, `.zip`, `.tar.gz`, `.tgz`).
* **Auto-Detection Engine (`AutoDetectRasterType`)**: Inspects GDAL metadata without full-pixel decoding to classify:
  * **Multi-Band (3–4 bands)**: Satellite / Ortho imagery, Color Relief, and Color Hillshade.
  * **16-bit Surface Reflectance Support**: Auto-contrast dynamic range stretching for Sentinel-2/Landsat 16-bit imagery.
  * **Single-Band (Float32 / UInt16 / Byte)**: Slope ($0^\circ\text{–}90^\circ$), Aspect ($0^\circ\text{–}360^\circ$), Roughness, Hillshade, and Elevation.
* **Duplicate & Incompatibility Rejection**: Validates paths and CRS/UTM alignment; immediately shows an interactive modal error dialog on duplicate selection.
* **Dynamic Layer Cards**: Color-coded badges, resolution/band stats, `Load` / `Unload` live scene toggles, individual delete (`✕`), and `Clear All`.

### 4. Interactive Slate Runtime HUD
* Left-anchored technical dark-theme control panel (`STerraDynePanel`):
  * **Live Progress Bar (`SProgressBar`)**: Real-time tile load tracking (`Loading DEM chunks: X / Total (Y%)`).
  * **Dynamic Overlay Stack**: Real-time list of added GIS layers with active emerald highlighting and `● ACTIVE` status badge.
  * **Top View Button**: One-click framing of active terrain footprints.
  * **Sculpting Accordion**: Brush selection (`Raise`, `Lower`, `Smooth`, `Flatten`), radius/strength sliders, and layer routing.
  * **Roads Accordion**: Spline node placement, road width adjustment, and mesh building with $\pm 200\text{ km}$ elevation raytracing.
  * **Buildings Accordion**: Footprint spline placement, floor stepper, and dynamic-step pad leveling.

### 5. Procedural Spline Roads & Junction Networks
* Interactive spline curve authoring with dynamic mesh generation.
* Automated N-way road intersection/junction detection and topological solving.
* Real-time road width scaling with automatic curb extrusion.
* Ground raytrace bounds expanded to $\pm 200\text{ km}$ to support extreme high-altitude Himalayan terrain ($3000\text{–}7000\text{ m}$).
* **Placeable on Tiled Cesium Import terrain**: TerraDyne's own chunk heightfield doesn't cover a locally-imported Cesium tile (see §9), so height sampling (`RoadMesh::SampleGroundHeight`) falls back to a $\pm 200\text{ km}$ line trace against the tile's real collision geometry whenever no TerraDyne chunk is under a station — same mechanism the ground raytrace above already used, just now able to tell TerraDyne terrain and "some other collidable ground" apart.
* **Gentle cross-slope banking over Cesium tile terrain**: rough roads have always banked to the terrain's lateral slope; paved roads stayed deliberately flat, since a flat road reads as correctly engineered on Sandbox/GDAL-imported terrain. Over a locally-imported Cesium tile specifically — real-world terrain that can be steep enough for a rigidly flat road to look wrong — a paved road now banks gently too (`MaxPavedCrossSlopeDegrees`, default $8°$), gated on the same fallback signal above so Sandbox/GDAL paved roads are completely unaffected.
* **Focus button**: frames the camera on the currently selected road (or road point) from the Roads panel section.
* **Delete Point removes the whole road once nothing's left**: a road can't render below two points, so deleting the last one deletes the road itself (re-solving any junction it was pinned to) instead of silently refusing — previously there was no way to remove a two-point stub road at all.
* **Junctions clean up after themselves**: deleting a connected road makes the junction re-solve from whatever's left; once fewer than two roads remain, the junction actor removes itself and un-pins the last survivor (if any) back to a normal, uncut road, instead of leaving stale, disconnected geometry behind. A freshly-placed junction with no roads drawn to it yet is unaffected — only one that had previously solved from real roads and then lost its connections is treated as orphaned.

### 6. Modular Spline Buildings & Dynamic Terraforming
* Arbitrary polygon footprint definition using spline nodes.
* Live floor count stepper controls.
* Macro chunk step-scaled pad terraforming (`EffectiveFalloff = Max(Falloff, Step * 1.5)`) ensuring smooth foundations on all grid resolutions.
* **Pad leveling on Tiled Cesium Import terrain**: `ResolvePadTargetWorldZ`'s footprint samples fall back to the same line-trace-against-collision approach as roads (above) wherever no TerraDyne chunk covers a sample point, so a building's pad still levels correctly when placed on a locally-imported Cesium tile instead of TerraDyne's own terrain.
* **Focus button**: frames the camera on the currently selected building from the Buildings panel section.

### 7. World Persistence & Transaction History
* Complete disk serialization format for terrain state, building transforms, and road splines.
* Multi-layer height buffers (**Base**, **Sculpt**, **Detail**).
* Full undo and redo transaction history (`Ctrl+Z`, `Ctrl+Y`).

### 8. 100% Multiplayer & Network Replication
* **Authoritative Server Architecture**: Strictly validated through `HasEditAuthority()`. Unauthorized edits are rejected on the server.
* **Delta-Broadcasting**: Raw $64 \times 64$ height buffers are deterministically evaluated locally; live sculpting strokes are multicast via lightweight geometric parameters (`Multicast_BroadcastBrushStroke`), consuming minimal bandwidth.
* **Full Actor & Property Replication**: `RasterLayerSlots`, `DynamicRasterLayers`, `BuildingActors`, and `RoadSplineActors` automatically synchronize with `DOREPLIFETIME` and `OnRep` handlers.
* **Multi-User Streaming**: Throttled view reporting (`Server_ReportViewLocation`) keeps server-side chunk streaming centered around all connected players.

### 9. Tiled Cesium Import (Local Quantized-Mesh Terrain & XYZ Imagery)
* **DEM-only**: only available once a world is Imported/GIS-DEM (or already has an elevation layer) — a real-world tile pyramid has no meaningful place next to Sandbox's procedural terrain, so the panel section and the underlying `ATerraDyneManager` calls both gate on it.
* **No hardcoded paths**: `Import Terrain Tileset Folder…` / `Import Imagery Overlay Folder…` open a native folder picker; the last folder picked per pyramid type is remembered across editor sessions via `GConfig` (`GEditorPerProjectIni`), never baked into source.
* **Terrain**: points a `ACesium3DTileset` (`FromUrl`) at a local Cesium `quantized-mesh-1.0` pyramid — a folder containing `layer.json` and `{z}/{x}/{y}.terrain` tiles.
* **Imagery**: adds a `UCesiumUrlTemplateRasterOverlay` reading a local XYZ PNG pyramid — files laid out on disk as `{z}/{x}/{y}.png`, requested via Cesium's `{reverseY}` template token (not `{y}`, which Cesium defines as TMS row-ordering — opposite of the standard XYZ/slippy-map ordering these QGIS-style exports actually use) — WebMercator projection; the zoom range is auto-detected from the pyramid's own numeric subfolder names, spawning a bare ellipsoid tileset first if no terrain has been imported yet.
* **Fully offline**: both use `file:///` URLs, which `UnrealAssetAccessor` short-circuits before ever reaching `FHttpModule` or Cesium Ion — no network access, no Cesium ion token required. Actors are spawned deferred (`SpawnActorDeferred`) with the tileset source set before `FinishSpawning()`, since `ACesium3DTileset` otherwise defaults to `FromCesiumIon` and calls `LoadTileset()` from `OnConstruction` immediately on spawn.
* **Auto-fly-to-footprint**: each successful import reads the pyramid's own geographic extent (from its deepest zoom level's actual tile index range, not the often-boilerplate `bounds` field in `layer.json`) and flies the camera to an aerial top-down view centred over it — the same framing angle as the existing "Top View" feature, altitude scaled to the footprint's size so it fits comfortably in frame.
* **Cuts a hole in the backdrop globe** (toggle: *Cut Out Backdrop Globe*, on by default): a successful terrain import clips every other `ACesium3DTileset` already in the world — e.g. a placed "Cesium World Terrain" used as the level's default globe backdrop — via a `UCesiumPolygonRasterOverlay` on a rectangle matching the imported footprint, so the backdrop stops rendering real terrain geometry for that ground (which would otherwise visibly overlap/z-fight with the local import) while staying fully visible everywhere else. Turn the toggle off if the computed footprint looks wrong and the cutout lands in the wrong spot. (Imagery-only imports don't touch this: the bare ellipsoid they spawn has no relief of its own to compete with the backdrop.)
* **Height Offset slider** (-500m to +500m): nudges the imported tiled layer (terrain, or the bare ellipsoid an imagery-only import spawns) up or down — for working around a residual vertical mismatch against the backdrop, or z-fighting right at the cutout edge. Absolute, not cumulative.
* **Separate render path**: this drapes a Cesium tileset actor alongside TerraDyne's own chunk terrain — it does not feed TerraDyne's chunk heightfield/sculpting pipeline (that remains `FTerraDyneGeoTIFFHeightSource`, driven by the GDAL GeoTIFF importer above).

---

## 🛠️ Requirements & Setup

### Engine & Plugin Dependencies
* **Unreal Engine**: `5.8`
* **Plugins Enabled**:
  * `GeometryScripting` & `GeometryFramework`
  * `VirtualHeightfieldMesh`
  * `CesiumForUnreal`
  * `CelestialVault` & `DaySequence`
  * `RoadBuilder` ([Plugins/RoadBuilder-master](Plugins/RoadBuilder-master)) — third-party spline road/junction editor (street, highway, elevated, tunnel styles; OpenDrive export), retargeted from its stock UE 5.3 release to 5.8. Depends on `GeoReferencing` and `PCG` (both enabled). Local build fixes on top of the vanilla plugin, all scoped to `Plugins/RoadBuilder-master`:
    * `Settings.h`/`Settings.cpp` renamed to `RoadBuilderSettings.h`/`.cpp` — the bare name collided with a new 5.8 engine header (`Engine/Source/Editor/WorldBookmark/.../Settings.h`) and UHT rejects duplicate header names project-wide.
    * `URoadLane` renamed to `URoadBuilderLane` — collided with `FRoadLane` from the MetaRoad plugin at the UHT reflected-name level (case/prefix-insensitive). No serialized content referenced the old name (only `FRoadLaneStyle`, which was already asset-mapped under a distinct name).
    * `RoadMarking.h`'s `DisallowedClasses` metadata now uses the long-form path `/Script/RoadBuilder.PolygonMarkStyle` — 5.8's UHT rejects short class names in that metadata key.
    * `RoadBuilderEditor.Build.cs` now lists `PCG` directly (previously relied on transitively inheriting it through the `RoadBuilder` module dependency), fixing an `LNK2001` on `PCG::Private::LogNotLoadedObjectNotOnGameThread` introduced by 5.8's PCG module.
    * `RoadBuilderSettings.cpp`'s `USettings_Global` constructor had a stray trailing `'` baked into the `DefaultGoreMarking` object path (`ChevronRegion.ChevronRegion'` instead of `ChevronRegion.ChevronRegion`) — a pre-existing upstream bug that only surfaces once the plugin's optional `Content.zip` (marking styles) is extracted into `Plugins/RoadBuilder-master/Content/`, since without that content the load was already failing for the unrelated reason of the asset not existing.
  * The plugin's marking-style/prop content ships separately from its GitHub source as `Content.zip` (see [Plugins/RoadBuilder-master/Readme.md](Plugins/RoadBuilder-master/Readme.md)) — extract it into `Plugins/RoadBuilder-master/Content/` to get the built-in road styles' referenced assets (crosswalks, chevron/crosshatch gore markings, props, etc.).

### GDAL Third-Party Setup
The DEM raster pipeline utilizes GDAL for geospatial raster parsing. To bootstrap vcpkg and build local GDAL binaries:

```powershell
./Tools/Setup-GDAL.ps1
```

### Building the Project
To compile the project via Unreal Build Tool (UBT):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" RealtimeTerrainEditor Win64 Development -Project="C:\Projects\RealtimeTerrain\RealtimeTerrain.uproject" -WaitMutex
```

---

## 🎮 Controls & Shortcuts

| Action | Control |
| :--- | :--- |
| **Orbit / Look** | Right Mouse Button + Drag |
| **Fly Movement** | `W` (Forward), `S` (Backward), `A` (Left), `D` (Right), `E` (Up), `Q` (Down) |
| **Flight Speed Adjust** | Mouse Wheel Scroll Up / Down |
| **Frame Whole Map (Top View)** | `F` or `Home` |
| **Terrain Sculpting** | Left Mouse Button + Drag on terrain |
| **Road Spline Authoring** | Left Click on terrain in Road mode |
| **Building Placement** | Left Click on terrain in Building mode |
| **Undo / Redo** | `Ctrl + Z` / `Ctrl + Y` |



