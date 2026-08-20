# RealtimeTerrain

## Third-party setup: GDAL

TerraDyne's Imported world mode (importing a GeoTIFF DEM and georeferencing it via Cesium)
reads the file through [GDAL](https://gdal.org/). There is no first-party UE5 GDAL plugin, so
this project builds GDAL locally from source with Microsoft's
[vcpkg](https://github.com/microsoft/vcpkg) rather than depending on a prebuilt binary from a
third-party mirror.

Before building the `TerraDyne` module, run:

```powershell
./Tools/Setup-GDAL.ps1
```

This clones and bootstraps vcpkg under `Tools/vcpkg/` (gitignored) and builds
`gdal:x64-windows`. First run takes roughly 30-60+ minutes and a few GB of disk -- it's
compiling GDAL and its own dependencies (PROJ, libtiff, zlib, sqlite3, and others), not just
downloading them. Safe to re-run; it skips steps that already succeeded.

`Plugins/TerraDyne-main/Source/ThirdParty/GDAL/GDAL.Build.cs` reads GDAL straight out of
`Tools/vcpkg/installed/x64-windows/` -- nothing else needs configuring afterward.

