# RealtimeTerrain (Unreal Engine 5.8)

A high-performance, runtime-editable chunk terrain and GIS framework for Unreal Engine 5.8. Built for large-scale geographic datasets, real-time procedural sculpting, multi-layer GIS/DEM raster ingestion, spline road networks, modular buildings, and world persistence.

---

## ⚡ Core Architecture

```text
       ┌─────────────────────────────────────────────────────────┐
       │                Session Mode Selection                   │
       └───────────────┬─────────────────────────┬───────────────┘
                       │                         │
               [SANDBOX MODE]            [GIS DEM MODE]
                       │                         │
            Procedural Fractal Grid         GDAL Multi-Layer Ingestion
            (Base / Sculpt / Detail)       (Elevation + 6 Derived Layers)
                       │                         │
                       │             Cesium GlobeAnchor Georeferencing
                       │             Adaptive Macro Chunk Sizing (~13.7km)
                       │             Zero High-Altitude Eviction
                       │                         │
                       └───────────┬─────────────┘
                                   ▼
       ┌─────────────────────────────────────────────────────────┐
       │                 Runtime Chunk Framework                 │
       ├─────────────────────────────────────────────────────────┤
       │ • High-Speed FDynamicMesh3 & Dynamic Collisions         │
       │ • Cesium DynamicPawn (WGS84, ESU Horizon, Zoom, TopView)│
       │ • Procedural Spline Roads & Junction Networks           │
       │ • Modular Spline Buildings & Terraforming Pads          │
       │ • Full Undo/Redo (Ctrl+Z/Y) & Disk Persistence          │
       │ • Slate Technical HUD with Live Progress & Layer Toggles│
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
  * **Dynamic Flight Speed**: Automatically scales maximum flying speed ($Altitude \times 2.5$) for effortless navigation from centimeters to regional scales.
  * **Sculpt Isolation**: Left-click drag is dedicated to terrain editing and spline drawing; right-click drag orbits freely without fighting camera controls.

### 3. Multi-Layer GIS GeoTIFF Ingestion & Macro Loading
* Ingests synchronized 7-layer raster products powered by local GDAL compilation:
  1. **Elevation (DEM)** — Reference grid establishing georeferenced bounds.
  2. **Hillshade** — Analytical surface relief shading.
  3. **Color Relief** — Hypsometric color-mapped elevation.
  4. **Color Hillshade** — Blended topographic color visualization.
  5. **Slope** — Gradient/incline angle mapping ($0^\circ–90^\circ$).
  6. **Aspect** — Slope azimuth/direction mapping ($0^\circ–360^\circ$).
  7. **Roughness** — Surface roughness variability.
* **Adaptive Chunk Scaling**: Dynamically sizes chunk footprints for large datasets (e.g. $435\text{ km} \times 291\text{ km}$ sized to $13.7\text{ km}$ chunks), loading hundreds of kilometers in **726 high-performance chunks** in seconds.
* **Zero High-Altitude Eviction**: Imported DEM chunks remain resident when orbiting at high altitudes.
* **High-Throughput Streaming**: Processes up to 16 chunk allocations per tick for instant dataset streaming.

### 4. Interactive Slate Runtime HUD
* Left-anchored technical dark-theme control panel (`STerraDynePanel`):
  * **Live Progress Bar (`SProgressBar`)**: Real-time tile load tracking (`Loading DEM chunks: X / Total (Y%)`).
  * **Per-Layer Controls**: Individual `Load` / `Unload` toggles and `✕` clear buttons for each of the 7 raster slots.
  * **Top View Button**: One-click framing of active terrain footprints.
  * **Sculpting Accordion**: Brush selection (`Raise`, `Lower`, `Smooth`, `Flatten`), radius/strength sliders, and layer routing.
  * **Roads Accordion**: Spline node placement, road width adjustment, and mesh building.
  * **Buildings Accordion**: Footprint spline placement, floor stepper, and leveling controls.

### 5. Procedural Spline Roads & Junction Networks
* Interactive spline curve authoring with dynamic mesh generation.
* Automated N-way road intersection/junction detection and topological solving.
* Real-time road width scaling with automatic curb extrusion.

### 6. Modular Spline Buildings & Terraforming
* Arbitrary polygon footprint definition using spline nodes.
* Live floor count stepper controls.
* Automatic terrain pad terraforming beneath structures to keep foundations level.

### 7. World Persistence & Transaction History
* Complete disk serialization format for terrain state, building transforms, and road splines.
* Multi-layer height buffers (**Base**, **Sculpt**, **Detail**).
* Full undo and redo transaction history (`Ctrl+Z`, `Ctrl+Y`).

### 8. 100% Multiplayer & Network Replication
* **Authoritative Server Architecture**: Strictly validated through `HasEditAuthority()`. Unauthorized edits are rejected on the server.
* **Delta-Broadcasting**: Raw $64 \times 64$ height buffers are deterministically evaluated locally; live sculpting strokes are multicast via lightweight geometric parameters (`Multicast_BroadcastBrushStroke`), consuming minimal bandwidth.
* **Full Actor & Property Replication**: `RasterLayerSlots`, `BuildingActors`, and `RoadSplineActors` automatically synchronize with `DOREPLIFETIME` and `OnRep` handlers.
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


