# RealtimeTerrain (Unreal Engine 5.8)

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
       │ • Cesium DynamicPawn (WGS84, ESU Horizon, Zoom, TopView)│
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
  * **Smooth Altitude-Scaled Zoom**: Mouse wheel zoom exponentially scales with camera altitude ($100\text{ m}$ ground level to $500\text{ km}$ orbit).
  * **Top-Down Map Framing (`F` / `Home` / HUD Button)**: Instantly computes active dataset bounds and frames the entire terrain footprint.
  * **Dynamic Flight Speed**: Automatically scales maximum flying speed ($\text{Altitude} \times 2.5$) for effortless navigation from centimeters to regional scales.
  * **Sculpt Isolation**: Left-click drag is dedicated to terrain editing and spline drawing; right-click drag orbits freely without fighting camera controls.

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

### 6. Modular Spline Buildings & Dynamic Terraforming
* Arbitrary polygon footprint definition using spline nodes.
* Live floor count stepper controls.
* Macro chunk step-scaled pad terraforming (`EffectiveFalloff = Max(Falloff, Step * 1.5)`) ensuring smooth foundations on all grid resolutions.

### 7. World Persistence & Transaction History
* Complete disk serialization format for terrain state, building transforms, and road splines.
* Multi-layer height buffers (**Base**, **Sculpt**, **Detail**).
* Full undo and redo transaction history (`Ctrl+Z`, `Ctrl+Y`).

### 8. 100% Multiplayer & Network Replication
* **Authoritative Server Architecture**: Strictly validated through `HasEditAuthority()`. Unauthorized edits are rejected on the server.
* **Delta-Broadcasting**: Raw $64 \times 64$ height buffers are deterministically evaluated locally; live sculpting strokes are multicast via lightweight geometric parameters (`Multicast_BroadcastBrushStroke`), consuming minimal bandwidth.
* **Full Actor & Property Replication**: `RasterLayerSlots`, `DynamicRasterLayers`, `BuildingActors`, and `RoadSplineActors` automatically synchronize with `DOREPLIFETIME` and `OnRep` handlers.
* **Multi-User Streaming**: Throttled view reporting (`Server_ReportViewLocation`) keeps server-side chunk streaming centered around all connected players.

---

## 🛠️ Requirements & Setup

### Engine & Plugin Dependencies
* **Unreal Engine**: `5.8`
* **Plugins Enabled**:
  * `GeometryScripting` & `GeometryFramework`
  * `VirtualHeightfieldMesh`
  * `CesiumForUnreal`
  * `CelestialVault` & `DaySequence`

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
| **Altitude Zoom** | Mouse Wheel Scroll Up / Down |
| **Frame Whole Map (Top View)** | `F` or `Home` |
| **Terrain Sculpting** | Left Mouse Button + Drag on terrain |
| **Road Spline Authoring** | Left Click on terrain in Road mode |
| **Building Placement** | Left Click on terrain in Building mode |
| **Undo / Redo** | `Ctrl + Z` / `Ctrl + Y` |

