# Features & Architecture

Detailed reference for what RealtimeTerrain does and how each part works. For a quick overview, see
the [README](../README.md); for how these systems were built and the bugs fixed along the way, see
[DEVELOPMENT_HISTORY.md](DEVELOPMENT_HISTORY.md).

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
       │ • Modular Spline Buildings & Dynamic-Step Pad Leveling  │
       │ • Full Undo/Redo (Ctrl+Z/Y) & Disk Persistence          │
       │ • Dynamic Slate HUD (Auto-Detect, Load/Unload, Delete)  │
       └─────────────────────────────────────────────────────────┘
```

---

## ✨ Features & Capabilities

### 1. Dual Independent Session Modes
* **Sandbox Mode**: Instant procedural grid initialization ready for immediate brush sculpting, layer editing, and building placement.
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
  * **World Bounds Checks disabled at startup**: `AWorldSettings::bEnableWorldBoundsChecks` is forced off in `UGRTSubsystem::Initialize` (before any actor's `BeginPlay`). Left on — Unreal's default — it's incompatible with Cesium's origin rebasing: a rebase shifts every actor's absolute Z, so an airborne pawn can spuriously read as having fallen below `KillZ`, get destroyed, and respawn at the default `PlayerStart` — visible as the camera snapping back mid-flight to wherever it started.

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
* Left-anchored technical dark-theme control panel (`SGRTPanel`):
  * **Live Progress Bar (`SProgressBar`)**: Real-time tile load tracking (`Loading DEM chunks: X / Total (Y%)`).
  * **Dynamic Overlay Stack**: Real-time list of added GIS layers with active emerald highlighting and `● ACTIVE` status badge.
  * **Top View Button**: One-click framing of active terrain footprints.
  * **Sculpting Accordion**: Brush selection (`Raise`, `Lower`, `Smooth`, `Flatten`), radius/strength sliders, and layer routing.
  * **Buildings Accordion**: Footprint spline placement, floor stepper, and dynamic-step pad leveling.

### 5. Modular Spline Buildings & Dynamic Terraforming
* Arbitrary polygon footprint definition using spline nodes.
* Live floor count stepper controls.
* Macro chunk step-scaled pad terraforming (`EffectiveFalloff = Max(Falloff, Step * 1.5)`) ensuring smooth foundations on all grid resolutions.
* **Pad leveling on Tiled Cesium Import terrain**: `ResolvePadTargetWorldZ`'s footprint samples fall back to a $\pm 200\text{ km}$ line-trace-against-collision approach wherever no GRT chunk covers a sample point, so a building's pad still levels correctly when placed on a locally-imported Cesium tile instead of GRT's own terrain.
* **Focus button**: frames the camera on the currently selected building from the Buildings panel section.

### 6. World Persistence & Transaction History
* Complete disk serialization format for terrain state and building transforms (scale and pad settings included).
* Multi-layer height buffers (**Base**, **Sculpt**, **Detail**).
* Full undo and redo transaction history (`Ctrl+Z`, `Ctrl+Y`, and the panel buttons): building/model place, delete, move/rotate/scale, footprint, floors, and OSM batches first, then terrain strokes. Loading a world clears it.
* Edited chunks that stream out are cached per world session and included in saves; the cache never carries edits into a different world.
* Save, load, and every file import are **host-only** — a client's path means nothing on the server's disk.

### 7. 100% Multiplayer & Network Replication
* **Authoritative Server Architecture**: Strictly validated through `HasEditAuthority()`. Unauthorized edits are rejected on the server.
* **Delta-Broadcasting**: Raw $64 \times 64$ height buffers are deterministically evaluated locally; live sculpting strokes are multicast via lightweight geometric parameters (`Multicast_BroadcastBrushStroke`), consuming minimal bandwidth.
* **Full Actor & Property Replication**: `RasterLayerSlots`, `DynamicRasterLayers`, and `BuildingActors` automatically synchronize with `DOREPLIFETIME` and `OnRep` handlers.
* **Multi-User Streaming**: Throttled view reporting (`Server_ReportViewLocation`) keeps server-side chunk streaming centered around all connected players.

### 8. Tiled Cesium Import (Local Quantized-Mesh Terrain & XYZ Imagery)
* **DEM-only**: only available once a world is Imported/GIS-DEM (or already has an elevation layer) — a real-world tile pyramid has no meaningful place next to Sandbox's procedural terrain, so the panel section and the underlying `AGRTManager` calls both gate on it.
* **No hardcoded paths**: `Import Terrain Tileset Folder…` / `Import Imagery Overlay Folder…` open a native folder picker; the last folder picked per pyramid type is remembered across editor sessions via `GConfig` (`GEditorPerProjectIni`), never baked into source.
* **Terrain**: points a `ACesium3DTileset` (`FromUrl`) at a local Cesium `quantized-mesh-1.0` pyramid — a folder containing `layer.json` and `{z}/{x}/{y}.terrain` tiles.
* **Imagery**: adds a `UCesiumUrlTemplateRasterOverlay` reading a local XYZ PNG pyramid — files laid out on disk as `{z}/{x}/{y}.png`, requested via Cesium's `{reverseY}` template token (not `{y}`, which Cesium defines as TMS row-ordering — opposite of the standard XYZ/slippy-map ordering these QGIS-style exports actually use) — WebMercator projection; the zoom range is auto-detected from the pyramid's own numeric subfolder names, spawning a bare ellipsoid tileset first if no terrain has been imported yet.
* **Fully offline**: both use `file:///` URLs, which `UnrealAssetAccessor` short-circuits before ever reaching `FHttpModule` or Cesium Ion — no network access, no Cesium ion token required. Actors are spawned deferred (`SpawnActorDeferred`) with the tileset source set before `FinishSpawning()`, since `ACesium3DTileset` otherwise defaults to `FromCesiumIon` and calls `LoadTileset()` from `OnConstruction` immediately on spawn.
* **Auto-fly-to-footprint**: each successful import reads the pyramid's own geographic extent (from its deepest zoom level's actual tile index range, not the often-boilerplate `bounds` field in `layer.json`) and flies the camera to an aerial top-down view centred over it — the same framing angle as the existing "Top View" feature, altitude scaled to the footprint's size so it fits comfortably in frame.
* **Cuts a hole in the backdrop globe** (toggle: *Cut Out Backdrop Globe*, on by default): a successful terrain import clips every other `ACesium3DTileset` already in the world — e.g. a placed "Cesium World Terrain" used as the level's default globe backdrop — via a `UCesiumPolygonRasterOverlay` on a rectangle matching the imported footprint, so the backdrop stops rendering real terrain geometry for that ground (which would otherwise visibly overlap/z-fight with the local import) while staying fully visible everywhere else. Turn the toggle off if the computed footprint looks wrong and the cutout lands in the wrong spot. (Imagery-only imports don't touch this: the bare ellipsoid they spawn has no relief of its own to compete with the backdrop.)

### 9. 3D Model Importer, Placement, Asset Palette & Scene Outliner
* **Runtime 3D Model Importers (`FGRTModelImporter`)**:
  * **6 Supported Formats**:
    * **Wavefront OBJ (`.obj` & `.mtl`)**: Positions, normals, UVs, n-gon fan triangulation, material groups, diffuse colors (`Kd`), and texture files (`map_Kd`).
    * **Autodesk FBX (`.fbx`)**: Runtime binary & ASCII FBX loading via the open-source **`ufbx`** library (bypassing Autodesk's editor-only SDK so FBX assets load in packaged standalone builds).
    * **glTF 2.0 & Binary glTF (`.gltf` & `.glb`)**: Complete JSON and binary container parsing via the open-source **`cgltf`** library, including PBR BaseColor materials and embedded/external PNG/JPG textures.
    * **Stereolithography STL (`.stl`)**: Both 80-byte header Binary and ASCII (`facet normal ... endfacet`).
    * **Polygon File Format PLY (`.ply`)**: Both ASCII and Binary header elements, vertex positions, normals, and face indices.
  * **Smart Multi-Source Texture Resolution (`FGRTModelImporter::ResolveTexturePath`)**:
    * Automatically resolves textures across FBX, glTF, and OBJ formats even when files contain broken or author-machine absolute paths (e.g. `C:\Users\Author\...`).
    * Searches candidate locations: exact path, model directory, adjacent subfolders (`textures/`, `Textures/`, `images/`, `maps/`, `materials/`), parent directories, and format fallbacks (`.png`, `.jpg`, `.bmp` for `.tga`/`.tif`/`.psd` references).
    * Embedded FBX binary textures decoded directly from internal memory blobs.
  * **Dynamic Image Wrapper**: Automatically loads `.png`, `.jpg`, and `.bmp` files (from external paths or embedded memory buffers) directly into transient `UTexture2D` instances.
  * **Textured Material Binding**: `ApplyMaterials()` instances the lit `/GRT/Materials/Models/M_GRT_ImportedModel` per slot and sets `BaseColor` (diffuse colour) and `BaseColorTexture` (diffuse texture; default white, so untextured slots show their colour). It falls back to `BasicShapeMaterial` — colour only — if that asset is missing.
  * **Automatic Normal & Bounds Computation**: Calculates bounding dimensions, polycounts (`tris • verts`), and overlays missing vertex normals via `FMeshNormals::InitializeOverlayToPerVertexNormals`.
* **Placed Model Actor (`AGRTPlacedModelActor`)**:
  * Runtime-spawnable, selectable 3D scene actor using `UDynamicMeshComponent` for geometry and collision raycasting.
  * **Multiplayer Replication**: Location, rotation, and `ReplicatedScale3D` synchronized across the network with `OnRep_Scale3D`.
  * **Visual State**: Custom depth stencil selection outline and scene visibility toggles (`SetVisibleInScene`).
* **Standard Transform Gizmo Integration (`UGizmoManipulationSubsystem`)**:
  * Seamlessly connects to the project's standard transform gizmo for Translate (`Shift+W`), Rotate (`Shift+E`), and Scale (`Shift+R`).
  * Full support for Coordinate Spaces (`C` for World/Local), Snapping (`G` / Ctrl-invert), and Cancel (`Escape`).
* **Slate HUD: 3D Models & Asset Library (`SGRTPanel`)**:
  * **Import Buttons**: `📥 Import Model...` (native OS multi-file picker for `*.obj;*.stl;*.ply;*.fbx;*.gltf;*.glb`) and `📁 Import Folder...` (batch folder import).
  * **Placeable Asset Palette**: Scrollable list of loaded models with **PNG photo thumbnails** (auto-resolved from companion `{ModelName}.png`, `{ModelName}_thumb.png`, or `preview.png`), dimensions in meters, polycount (`tris • verts`), one-click `[Place]` button, and `[X]` removal.
  * **Terrain Placement Mode**: Shows a holographic preview ghost following the cursor projected onto terrain chunks. Clicking drops and spawns an `AGRTPlacedModelActor` and attaches the transform gizmo immediately.
* **Slate HUD: Scene Outliner / Hierarchy Panel**:
  * Live object counter header (e.g. `SCENE OUTLINER (7)`).
  * Complete hierarchy listing placed 3D models (`[Model]`) and procedural buildings (`[Building]`).
  * **Row Actions**:
    * **Click Label / Row**: Selects the entity in the viewport and attaches the standard transform gizmo.
    * **`🎯` Focus**: Smoothly flies the camera to center and frame the object.
    * **`👁️` Visibility**: Toggles visibility for everyone in the session via `AGRTEditController::RequestSetActorVisible` (replicated; a hidden model also stops blocking clicks).
    * **`🗑️` Delete**: Destroys the entity and removes it from the world (undoable).


**Known limitations**: multiplayer needs the model file present on each machine (inherent to replicating a
path rather than geometry); generated thumbnails need a real RHI and have not been inspected
visually, only proven to no-op safely headless.

---

### 10. OpenStreetMap Data Ingestion

Imports real-world buildings from an OpenStreetMap export. Export an area from
openstreetmap.org (pan to it, click **Export**), then use **🌍 Import OpenStreetMap (.osm)…** in the
new **OPENSTREETMAP DATA** panel section. Offline and packaged-build safe — no network dependency.

* **Reader (`FGRTOSMParser`)** — OSM XML via the engine's runtime `XmlParser` module, so no
  third-party dependency. Nodes, ways and tags; `<relation>` is deliberately ignored for now, since
  what it mainly buys is multipolygon buildings with holes and the footprint spline cannot express
  a hole. Deliberately pure — no world, no UObject, no georeferencing — so the fiddly decisions are
  testable with no map open.
  * **Classification**: closed `building=*` ways become buildings (`building=no` excluded).
  * **Tag interpretation**: `building:levels` → storeys, falling back to `height` in metres over a
    3 m storey. Numeric tags are free text in OSM and routinely carry units (`"12 m"`), so the
    leading number is taken and the rest ignored rather than rejecting the tag.
  * **Line-oriented XML workaround**: `FXmlFile` cannot parse two elements sharing a line and
    rejects the whole document with a bare "Failed to parse the loaded document". Exports from
    openstreetmap.org are one element per line and load as-is, but anything minified failed for a
    reason a user could never act on, so the reader splits element boundaries onto their own lines
    first. Safe here specifically because OSM XML holds all its data in attributes, never in element
    text, and a literal `><` cannot occur inside a properly escaped attribute value.
* **Geometry (`FGRTOSMIngest`)** — also pure. Drops OSM's repeated closing node and any
  coincident corners, centres each footprint's origin on its centroid (so moving a building stays
  one transform), and **normalises footprint winding counter-clockwise**.
  * That last one is not cosmetic. `ResetToDefaultFootprint` winds counter-clockwise deliberately,
    and wall generation and the terrain pad both read that ordering — a clockwise footprint builds
    the building inside-out. It also cannot be decided from OSM's own ring convention, because this
    project georeferences through Cesium in **East-South-Up**: latitude grows toward −Y, so the
    conversion flips every ring's handedness on the way in. Normalising *after* conversion, in
    Unreal space, is the only ordering that is actually correct.
* **Placement** — lon/lat goes through the level's `ACesiumGeoreference` via
  `TransformLongitudeLatitudeHeightPositionToUnreal`, the same call the GeoTIFF and tiled-Cesium
  paths already use. The import refuses to run without a georeference rather than guessing.
  Buildings resolve their own ground height through `SnapToPadHeight`.
* **Truncation is reported, not silent** — a city-sized extract holds hundreds of thousands of ways,
  so imports stop at `MaxFeatures` (2000) and the summary says how many were left behind, alongside
  counts of anything skipped for unresolvable node refs or too few usable points.

Covered by seven tests: `GRT.OSM.Parse`, `.Classify`, `.TagResolution`, `.MalformedInput`,
`.FootprintWinding`, `.FootprintBuild` and `.Centreline`. Full suite **115/116**, the sole failure
being the pre-existing `GRT.Raster.Workflow` fixture gap.

**Not yet done**: downloading an area straight from the OSM API (the reader already takes a string,
so it is additive); `.osm.pbf`; multipolygon relations; and the end-to-end spawn path has unit
coverage on every pure layer but has not been run against a real georeferenced level.
