# RealtimeTerrain (Unreal Engine 5.8)

A high-performance, runtime-editable chunk terrain framework for Unreal Engine 5.8. Supports real-time sculpting, multi-layer GIS/DEM raster ingestion, procedural spline road generation, modular spline buildings, and world persistence without voxelization.

---

## ⚡ Core Architecture

```text
       ┌───────────────────────────────┐
       │   Session Mode Selection      │
       └───────┬───────────────┬───────┘
               │               │
      [SANDBOX MODE]    [DEM IMPORT MODE]
               │               │
    Procedural Grid       GDAL GeoTIFF Ingestion
    (Base/Sculpt/Detail)  (Elevation + 6 Derived Layers)
               │               │
               └───────┬───────┘
                       ▼
       ┌───────────────────────────────┐
       │   Runtime Chunk Framework     │
       ├───────────────────────────────┤
       │ • Dynamic Mesh & Collision    │
       │ • Multi-Layer Buffer Blending │
       │ • Spline Roads & Buildings    │
       │ • Undo/Redo & Save System     │
       └───────────────────────────────┘
```

---

## ✨ Features

### 1. Dual Session Modes
* **Sandbox Mode**: Instant procedural grid initialization ready for immediate sculpting, layer editing, road drawing, and building placement.
* **DEM Import Mode**: Clean, dedicated GIS workflow that loads zero procedural chunks on startup and awaits user-provided GeoTIFF datasets.

### 2. Multi-Layer GeoTIFF / GIS Ingestion
* Ingests synchronized 7-layer raster stacks powered by local GDAL compilation:
  1. **Elevation (DEM)** — Required reference grid establishing georeferenced bounds.
  2. **Hillshade** — Surface relief shading.
  3. **Color Relief** — Hypsometric color-mapped elevation.
  4. **Color Hillshade** — Blended topographic visualization.
  5. **Slope** — Gradient/incline angle mapping.
  6. **Aspect** — Slope azimuth/direction mapping.
  7. **Roughness** — Micro-topographic variability.
* Seamless Cesium georeference origin alignment.
* Support for single GeoTIFF files or compressed multi-raster archives.

### 3. Non-Destructive Layered Sculpting
* Multi-layer height buffer pipeline:
  * **Base**: Foundation heightmap data.
  * **Sculpt**: User deformations and live brush modifications.
  * **Detail**: Fine-grain micro-adjustments.
* Real-time brush operations: **Raise**, **Lower**, **Smooth**, and **Flatten**.
* Configurable brush radius and strength with smooth falloff.
* Full undo and redo transaction history.

### 4. Interactive Spline Roads
* Procedural curved road network authoring.
* Adjustable road width slider with automatic curb extrusion and trail generation.
* Interactive spline manipulation with point insertion, deletion, and spline resets.

### 5. Modular Spline Buildings
* Custom footprint spline definition for structures.
* Live floor count stepper controls.
* Automatic terrain pad terraforming beneath structures to keep ground leveled.

### 6. Native Slate Runtime HUD
* Left-anchored technical dark-theme control panel.
* Live session mode switcher badge (`DEM` / `SANDBOX`).
* Collapsible accordion sections:
  * **Levels**: Save to file / Load from file.
  * **GeoTIFF / Raster Layers**: Layer stack management, individual replacement/unloading, and scene ingestion.
  * **Sculpting**: Brush selection, layer routing, and radius/strength sliders.
  * **Roads**: Draw/Select modes, width adjustment, and spline controls.
  * **Buildings**: Place/Select modes, floor stepper, and footprint spline controls.

### 7. World Persistence & Chunk Streaming
* Complete disk serialization format for terrain state, building transforms, and road splines.
* Distance-based chunk streaming with asynchronous dirty-buffer saving.

---

## 🛠️ Requirements & Setup

### Unreal Engine Plugins
* Unreal Engine `5.8`
* `GeometryScripting`
* `VirtualHeightfieldMesh`
* `CelestialVault` & `DaySequence`

### GDAL Third-Party Setup
The DEM raster pipeline utilizes GDAL for geospatial parsing. To compile the local GDAL dependencies via vcpkg:

```powershell
./Tools/Setup-GDAL.ps1
```

---

## 🎮 Runtime Controls

* **Switch Session Mode**: Click the `DEM` / `SANDBOX` badge in the HUD header.
* **Sculpting**: Select a brush mode (`Raise`, `Lower`, `Smooth`, `Flatten`) and drag across terrain in the viewport.
* **Roads**: Select `Draw`, click on terrain to drop spline points, and adjust `ROAD WIDTH`.
* **Buildings**: Select `Place`, click to position footprint spline points, and use `+` / `–` to adjust floor count.
