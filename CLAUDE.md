# RealtimeTerrain — Unreal Engine 5.8

Runtime-editable chunk terrain + GIS framework: procedural/GDAL DEM terrain, Cesium georeferencing,
spline road networks, modular buildings, full multiplayer replication, and a reusable runtime transform gizmo.
Full status and historical record live in [README.md](README.md) — that file is the source of truth for
what's done, what's known-limited, and why; don't duplicate it here.

**Exhaustive per-file code reference** (every `.cpp`/`.h` in both modules, plus project-original
Blueprint/Material Content assets, documented from actually reading each one — not summarized here):
[Source/RealtimeTerrain/CODE_REFERENCE.md](Source/RealtimeTerrain/CODE_REFERENCE.md) and
[Plugins/GRT/CODE_REFERENCE.md](Plugins/GRT/CODE_REFERENCE.md). Consult these
before assuming what a specific file does — the Architecture map below is a navigation aid, not a
substitute.

---

## 🛑 Ground Rules (Non-Negotiable)

- **Read the actual file before proposing a change to it.** Not a grep snippet, not memory of a
  prior session. If asked to analyze specific code, open it in full first.
- **Verify API/behavior claims against real source**, not general UE knowledge. This codebase has
  custom systems (`AGRTEditController`, `RuntimeGizmoActor`, the road solver) that don't match
  stock UE patterns, and the RoadBuilder plugin has been locally patched (see README's Requirements
  section) — don't assume the vanilla plugin behavior.
- **Check what a fix could break elsewhere** before calling it done — a caller, another mode, or
  replication. A green build is not the same as "no regression."
- **Always use native OS file dialogs for importing/exporting.** Always use `IDesktopPlatform::OpenFileDialog`, `OpenDirectoryDialog`, and `SaveFileDialog` (via `FDesktopPlatformModule::Get()`) for all file and folder browsing (GeoTIFF, DEM, OSM, 3D Models, level saves). NEVER default to or pop up in-game Slate file picker UIs (`SGRTFilePickerPanel`) in editor/desktop workflows.
- **Use MCP for live testing when Unreal Editor is open.** Epic's official `ModelContextProtocol`
  server runs on `http://127.0.0.1:8000/mcp`. Use it to trigger Live Coding compiles and execute
  automation tests live without restarting the editor.

---

## ⚡ Build & Compilation Workflows

### 1. When Unreal Editor is Running (Live Coding via MCP)
Do not kill the editor. Trigger Live Coding compiles directly through MCP or via the in-editor shortcut:
- **MCP API**: Call `LiveCodingToolset.LiveCodingToolset.CompileLiveCoding` on `http://127.0.0.1:8000/mcp`.
- **Keyboard Shortcut**: Press `Ctrl + Alt + F11` inside the Unreal Editor.

### 2. When Unreal Editor is Closed (Headless UBT Build)
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" RealtimeTerrainEditor Win64 Development -Project="C:\Projects\RealtimeTerrain\RealtimeTerrain.uproject" -WaitMutex
```

---

## 🧪 Testing & Automation

### 1. Live Testing via ModelContextProtocol (Editor Open)
Use Python to communicate with the embedded MCP server on port 8000 to compile and run tests instantly:

```python
import urllib.request, json

def post_mcp(data, session_id=None):
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json'}
    if session_id:
        headers['Mcp-Session-Id'] = session_id
    req = urllib.request.Request('http://127.0.0.1:8000/mcp', data=json.dumps(data).encode('utf-8'), headers=headers)
    with urllib.request.urlopen(req) as resp:
        return resp.headers.get('Mcp-Session-Id'), resp.read().decode('utf-8')

# 1. Initialize session
session_id, _ = post_mcp({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'antigravity', 'version': '1.0'}}})
post_mcp({'jsonrpc': '2.0', 'method': 'notifications/initialized'}, session_id)

# 2. Trigger LiveCoding (if code changed)
_, compile_res = post_mcp({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call', 'params': {'name': 'call_tool', 'arguments': {'toolset_name': 'LiveCodingToolset.LiveCodingToolset', 'tool_name': 'CompileLiveCoding', 'arguments': {}}}}, session_id)
print('Compile:', json.loads(compile_res)['result']['content'][0]['text'])

# 3. Discover & Run Automation Tests (e.g. GRT.Gizmo)
post_mcp({'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {'name': 'call_tool', 'arguments': {'toolset_name': 'AutomationTestToolset.AutomationTestToolset', 'tool_name': 'DiscoverTests', 'arguments': {'bForceRediscover': True}}}}, session_id)
_, test_res = post_mcp({'jsonrpc': '2.0', 'id': 4, 'method': 'tools/call', 'params': {'name': 'call_tool', 'arguments': {'toolset_name': 'AutomationTestToolset.AutomationTestToolset', 'tool_name': 'RunTestsByFilter', 'arguments': {'filterExpression': 'GRT.Gizmo'}}}}, session_id)
print('Test Results:', json.loads(test_res)['result']['content'][0]['text'])
```

### 2. Headless Testing (Editor Closed)
```powershell
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Projects\RealtimeTerrain\RealtimeTerrain.uproject" -ExecCmds="Automation RunTests GRT.Gizmo;Quit" -unattended -nopause -nosplash -nullrhi -log
```
Then inspect `Saved/Logs/RealtimeTerrain.log` (grep for `Test Started`/`Test Completed`/`Result=`).

*Note*: The one known pre-existing failure across the whole suite is `GRT.Raster.Workflow` (missing satellite/ortho test fixture, unrelated to active code) — do not chase it.

---

## ⚠️ Unreal Engine 5.8 & C++20 Critical Gotchas

1. **`TObjectPtr<T>` vs. Raw `T*` Ambiguity (`C2445`)**:
   - In MSVC / C++20, ternary expressions mixing `TObjectPtr<T>` and raw pointer `T*` fail with `error C2445: result type of conditional expression is ambiguous`.
   - **Fix**: Always call `.Get()` on `TObjectPtr` or `FObjectFinder::Object`, or use explicit `if / else` blocks instead of nested ternaries.
   - Example:
     ```cpp
     // BAD - causes C2445
     UMaterialInterface* Mat = MatInstX ? MatInstX : (MatX ? MatX.Get() : BaseMat);
     // GOOD
     UMaterialInterface* Mat = MatX ? MatX.Get() : (MatInstX ? MatInstX.Get() : BaseMat);
     ```

2. **Asset Path Fallbacks**:
   - Content assets relocated between `/Game/Gizmo/` and `/Game/Mytaverse/TransformGizmo/` must have dual-path lookups in constructors (`FObjectFinder`) and runtime (`LoadObject`).
   - Material shaders (`M_GizmoAxis`, `M_ScreenRotation`) depend on `MF_OccludedPixels` in `Content/Mytaverse/` — do not delete that vendor content.

3. **Multiplayer Scale Replication**:
   - Unreal's standard `FRepMovement` replicates Location and Rotation but **omits `Scale3D`**.
   - Actors supporting runtime scaling (e.g. `AGRTBuildingActor`) must declare a dedicated `ReplicatedScale3D` with `OnRep_Scale3D`.

4. **Cesium Coordinate System**:
   - Cesium georeferencing is **East-South-Up (ESU)**. Do not assume standard Unreal +Y=North; `-Point.Y` represents the North bound.

---

## 📐 Transform Gizmo Architecture & Invariants

- **Component & Handle Reach Constants** (`ARuntimeGizmoActor`):
  - `ShaftDistance = 65.0f`, `TipDistance = 160.0f` (Arrow reaches 160cm).
  - `PlaneHandleOffset = 45.0f`, `PlaneHandleHalfSize = 11.0f` (Midway along axes).
  - `ScaleHandleDistance = 150.0f`, `NativeRingRadius = 92.0f`, `RotateRingScale = 1.5f`.
  - `ScreenRingScaleMultiplier = 1.22f` (Outer screen-rotation ring radius ~168cm).
- **Primary Axis Orientations & Placement**:
  - Cylinder & Cone arrow meshes: Primary axis is **Z** (`FRotationMatrix::MakeFromZ`).
  - `SM_ScaleHandle`: Primary axis is local **X** (`FRotationMatrix::MakeFromX`), rooted at `(0, 0, 0)` with scale `1.2` reaching 150cm.
  - `SM_UniformScaleHandle`: Rooted at `(0, 0, 0)` with scale `1.8` (25cm cube at origin).
  - `SM_RotationHandle`: Ring normal is local **X** (`FRotationMatrix::MakeFromX`), scaled `1.5` (radius 138cm).
  - `SM_ScreenRotationHandle`: Dedicated outer ring (radius 168cm) billboarded in `Tick()` to face camera (`MakeFromX(ToCamera)`).
- **Planar Handles**:
  - `PlaneYZ` (between +Y and +Z): **Red** (`MI_XAxis` / `MatPlaneYZ`), spans Y & Z axes.
  - `PlaneXZ` (between +X and +Z): **Green** (`MI_YAxis` / `MatPlaneXZ`), spans X & Z axes.
  - `PlaneXY` (between +X and +Y): **Blue** (`MI_ZAxis` / `MatPlaneXY`), spans X & Y axes.
- **Center Handle**:
  - Uses `SM_ScreenTranslationHandle` (concentric circular disk).
  - Billboards every frame in `Tick()` using `CamLoc - GetActorLocation()` so it always faces the camera.
- **Hit Testing (`UGizmoManipulationSubsystem`)**:
  - Uses exact mathematical distance checks (`DistanceRayToSegment`, `DistanceRayToRing`, `DistanceRayToQuad`, `DistanceRayToPoint`).
  - Rejects handles behind ray origin (`AlongRay >= 0.0`).
  - Never uses physics collision traces for gizmo handles.

---

## 🗺️ Architecture Map

- **`Source/RealtimeTerrain/`** — game module.
  Contains project-specific game mode (`RealtimeTerrainEditGameMode`) and edit controller (`RealtimeRoadEditController`) inheriting directly from `AGRTEditController`.
- **`Plugins/GRT/Source/GRT/`** (157 files across `GRT` + `GRTEditor` —
  full detail in its `CODE_REFERENCE.md`), organized by subfolder:
  - `Road/` — complete, self-contained runtime road network and geometry system: `ARealtimeRoadSplineActor` (swept solid cross-section road actor with dynamic conform, elevation/width keys, spiral centreline, piers, and scatter HISM), `ARealtimeRoadJunctionActor` (multi-way N-arm junction solver, gore markings, fillet arcs, and ramp classification), `URealtimeRoadBuilderComponent` (runtime click-and-drag road drafting), `RoadCurveGraph` (Hermite offset curve graph & lane boundaries), `RoadNetwork` (spatial intersection sweeps & auto-junction creation), `RoadNetworkSolver` (fillet geometry, ray-intersection fallback, gore apex, lane routing), `RoadMeshBuilder` (style-driven cross-section sweeping & gore fill), `RoadElevationProfile` (cubic-Hermite elevation keys & auto-flyovers), `RoadWidthProfile` (Scale-mode width keys), `RoadSpiral` (Euler-spiral/clothoid Fresnel integrals), `RoadPierPlacement`, `RoadStylePresets`, `RoadXodrExport` (OpenDRIVE export), `RoadEditOverlayActor`/`RoadNetworkDebugLibrary`.
  - `Building/` — `AGRTBuildingActor`, `AGRTPlacedModelActor` (runtime placed 3D model instance with `UDynamicMeshComponent`, replicated `Scale3D`, visibility toggles, and selection highlight), `AGRTGizmoActor` (footprint-spline-vertex handles only), `GRTModelTypes.h` (`FGRTModelLibraryEntryData`/`FGRTPlacedModelData` — the save-game and undo-snapshot shapes for the model catalog and its placements), `GRTBuildingKit.h`/`GRTBuildingTypes.h` (building-kit data types).
  - `Gizmo/` — the reusable transform gizmo: `GizmoTypes.h` (enums/state), `GizmoManipulationMath.h/.cpp` (pure math, no UObject refs), `ARuntimeGizmoActor` (visual handles for Translate, Rotate, Scale with screen-rotation ring), `UGizmoManipulationSubsystem` (`UWorldSubsystem`, single stateful owner). Drives buildings, placed 3D models, and road points identically.
  - `IO/` — `GRTModelImporter` (zero-external-dependency runtime loader for `.obj`, `.stl`, `.ply`, `.fbx`, `.gltf`, `.glb` + `.mtl` diffuse colors and companion `.png`/`.jpg` textures into `FDynamicMesh3`, via vendored `ufbx`/`cgltf` under `Private/ThirdParty/`), `GRTModelThumbnail` (runtime scene-capture thumbnail generation, cached to `Saved/GRT/ModelThumbnails/`), `GRTOSMParser`/`GRTOSMIngest` (OpenStreetMap `.osm` XML parsing + geometry ingest — buildings and drivable roads, pure/world-free), `GRTSerializer`/`GRTAsyncSaver`/`GRTArchiveHelper` (save-game I/O), `GRTGeoTIFFReader`/`GRTGeoTIFFHeightSource`/`GRTDEMMeshBuilder`/`GRTRasterLayers` (GDAL/DEM import pipeline).
  - `Core/` — `AGRTManager`, `UGRTAssetLibrarySubsystem` (`UWorldSubsystem` catalog of imported 3D models with PNG thumbnail resolution, dimensions, and polycounts), `UGRTSubsystem`, `AGRTEditController` (runtime placement tool `PlaceModel`, raycast selection of placed models, holographic terrain ghosting, gizmo integration, OpenStreetMap import — frame-time budgeted 4.0ms tick draining, O(1) DEM height lookups, deferred batch junction resolution, its own building/model/road/OSM undo stack), `AGRTEditPawn`, `AGRTWorldPreset`, `AGRTProceduralGenerator`, `UGRTSaveGame`/`GRTWorldTypes.h`.
  - `World/` — `AGRTChunk` (the sculptable `FDynamicMesh3` + VHFM chunk actor),
    `AGRTOrchestrator`, `GRTSceneSetup`, `GRTDemoMarkerActor`/`GRTProjectile`.
  - `UI/` — `SGRTPanel` (the left-anchored Slate HUD with collapsible **"3D Models & Assets"** palette displaying **PNG photo thumbnails** and **"Scene Outliner"** with live item count, focus `🎯`, visibility `👁️`, and deletion `🗑️`, plus **"OpenStreetMap Data"** import with a progress bar), `SGRTFilePickerPanel` (extension-filterable — GeoTIFF and OSM both reuse it), `SGRTWorldModeChoicePanel`.
  - `Physics/` — `GRTCollision` (chunk raycast/traceability), `GRTMeshUtils`.
  - `Settings/` — `UGRTSettings`. Module entry: `GRTModule.cpp`.
  - `GRTEditor/` (separate module) — `GRTBaker`, `GRTFactory`, `SGRTTools`.
  - `Private/Tests/` — all `GRT.*` automation tests (45 test files, including 37 `GRT.Road.*` tests, `GRTModelImportTests.cpp`, `GRTOSMParserTests.cpp`, `GRTOSMIngestTests.cpp`, `GRTActorUndoTests.cpp`).
  - **Content dependency gotcha, verified by reading the actual asset binaries**: the Gizmo materials
    (`Content/Gizmo/Materials/M_GizmoAxis.uasset`, `M_ScreenRotation.uasset`) reference
    `MF_OccludedPixels`, which lives in `Content/Mytaverse/` — the third-party demo pack. Deleting
    `Mytaverse/` as "unused vendor content" would silently break gizmo rendering.
- **`Plugins/RoadBuilder-master/`** — third-party plugin, retargeted from UE 5.3 to 5.8 with local
  patches scoped entirely to this directory (renamed headers/classes to dodge UHT collisions — see
  README's Requirements section for the exact list before touching it). Its marking-style content
  ships separately as `Content.zip`, not in source control.
- **`Content/`** — mostly third-party marketplace packs (`SD_Art` modular buildings: 749 assets,
  `UnrealDrive_CitySample`: 252, `MWLandscapeAutoMaterial`: 97, `Mytaverse`: 44), not this project's
  own work. Project-original folders are `Content/Gizmo/`, `Content/Roads/`, `Content/Materials/`,
  `Content/CesiumSettings/` — their Blueprint/Material assets are documented in the two
  `CODE_REFERENCE.md` files above. **Git LFS gotcha**: some assets (12 found in
  `Plugins/GRT/Content/` alone) can sit as never-downloaded LFS pointer stubs (~130-byte
  text files, not real content) rather than actual binaries — check `git lfs ls-files` (`-` = not
  downloaded) before assuming a `.uasset` on disk is real, and run `git lfs pull --include="<path>"`
  to fetch what's missing.

---

## 🔒 Established Conventions

- Every new road/gizmo feature defaults to prior behavior when unset — no existing road/building
  changes behavior as a side effect of new work.
- Drags/deltas use **initial transform + accumulated delta**, never compounding onto an
  already-modified transform.
- Undo is delegate-based (`OnManipulationBegin/End/Cancel`), not `FScopedTransaction` — that's
  editor-only and wouldn't survive a packaged build.
- Cesium georeferencing here is **East-South-Up (ESU)**, not the common assumption of +Y=North —
  `-Point.Y` for a North bound is correct, not a bug.
- A geometry/mesh claim needs a test that checks the actual downstream contract (winding, ordering,
  normals, measured world-space values) — "doesn't crash" has hidden real bugs before.

---

## 🎨 Mandatory UX & Interaction Design Principles

1. **Jakob’s Law & Platform Familiarity (Never Reinvent OS Primitives)**:
   - Always use native OS file and directory dialogs (`IDesktopPlatform::OpenFileDialog`, `OpenDirectoryDialog`, `SaveFileDialog`) for loading, importing, and saving. Never force users to navigate folder trees inside an in-engine Slate UI.
   - Adhere to industry-standard 3D tool conventions: `Shift+W` (Translate), `Shift+E` (Rotate), `Shift+R` (Scale), `F` (Focus/Frame Selected), `Alt+Drag` (Duplicate), `Escape` (Cancel tool/selection).

2. **Hick’s Law & Progressive Disclosure**:
   - Do not overwhelm the HUD or viewport with dozens of raw numeric sliders or technical debug matrices.
   - Provide high-level, human-friendly presets with sensible defaults (e.g. "2-Lane Asphalt", "4-Lane Highway", "T-Junction"); hide deep geometric tuning inside clean collapsible accordion panels.

3. **Visibility of System Status (Nielsen Heuristic #1)**:
   - Every background task (OSM parsing, DEM/GeoTIFF raster loading, road spline baking, model importing) must provide immediate feedback (status line, progress bar, or holographic preview ghost).
   - Never freeze the game thread or leave interaction buttons in unresponsive/silent states without clear error messaging.

4. **Forgiving Interactions & Non-Destructive Workflows**:
   - Every spatial transformation, road edit, or model placement must support cancellation (`Escape`) and clean undo (`Ctrl+Z`).
   - Edits must never destroy underlying terrain or assets without explicit non-destructive layers (e.g. dynamic reversible terrain pads instead of permanent destructive sculpts).

5. **Fitts’s Law & Spatial Ergonomics**:
   - Gizmo handles and hit-testing discs must have generous grab reach ($Reach + Tolerance$), view-plane billboarding, and screen-space camera scaling so they remain effortlessly clickable whether zooming in close or flying high.
   - Flight pawn speeds must scale gracefully with altitude ($20\text{ m/s}$ at ground level) rather than launching at uncontrollable hypersonic speeds.

