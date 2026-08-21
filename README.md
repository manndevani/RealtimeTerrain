# RealtimeTerrain (Unreal Engine 5.8)

A high-performance, runtime-editable chunk terrain framework for Unreal Engine 5.8. Supports real-time sculpting, multi-layer GIS/DEM raster ingestion, procedural spline road generation, modular spline buildings, and world persistence without voxelization.

---

## 🛠️ Third-Party Dependencies: GDAL

The DEM raster import mode (importing GeoTIFF elevation stacks and georeferencing via Cesium) processes geospatial raster data through GDAL. GDAL is built locally from source with vcpkg.

Before building the project module:

```powershell
./Tools/Setup-GDAL.ps1
```

This bootstraps vcpkg under `Tools/vcpkg/` and builds `gdal:x64-windows` alongside all required dependencies (PROJ, libtiff, zlib, sqlite3).

---

## ⚡ Core Capabilities

* **Dual Session Modes**: Sandbox mode (procedural grid) & DEM Import mode (multi-raster GIS ingestion).
* **Multi-Layer Height Editing**: Non-destructive Base, Sculpt, and Detail layer buffers.
* **Procedural Roads & Networks**: Interactive spline curved roads with width control and curb extrusion.
* **Modular Spline Buildings**: Footprint spline definition, floor stepper, and automatic terrain pad flattening.
* **Pure Slate HUD**: Left-anchored technical dark-theme control panel with live mode switching.
* **Disk Persistence**: Chunk streaming caching, binary serialization, and undo/redo stacks.


