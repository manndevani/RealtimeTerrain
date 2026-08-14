# Changelog

## [0.3] - 2026-04-05
### Added
- **Authored World Conversion:** GitHub now includes the verified landscape migration path with paint layer capture, placed foliage transfer, and actor foliage transfer.
- **Persistent Runtime World:** Terrain save/load, replicated chunk sync, persistent runtime actors, destruction state, harvest/regrowth flow, and runtime placement are part of the public release.
- **Procedural Extension:** Seeded outskirts, biome overlays, runtime spawn rules, optional edge growth, and PCG-ready point export are now included.
- **Gameplay Hooks:** Blueprint-callable biome queries, AI spawn zones, build-permission checks, navmesh dirtying, and terrain/foliage/population change events are exposed from the manager.
- **Designer Workflow:** World presets, scene templates, showcase automation, and packaged docs now ship with the plugin.

### Changed
- **Public Positioning:** TerraDyne 0.3 is now presented as a persistent runtime world framework for survival, sandbox, and open-world games built on Unreal Landscapes.
- **Release Versioning:** The GitHub plugin manifest and shipped UI strings now report the public release as `0.3`.

### Verified
- **Build:** `TerraDyneEditor` builds cleanly on Unreal Engine 5.7.
- **Automation:** `Automation RunTests TerraDyne` passes with 16 TerraDyne tests.

## [1.2.1] - 2026-03-14
### Fixed
- **Startup Safety:** Default terrain spawning, scene lighting, and showcase playback are now opt-in instead of mutating every world on `BeginPlay`.
- **Self-Contained Defaults:** Default material and tool UI paths no longer depend on `/Game/...` project assets.
- **Undo Reliability:** Snapshot restore now validates terrain buffer sizes before rebuilding meshes, preventing undo crashes on malformed data.
- **Chunk Registration:** Chunks register and unregister with the manager automatically, which keeps the active chunk map valid for pre-placed and streamed actors.
- **Network Sync Resilience:** Client chunk sync messages are queued until the target chunk exists, improving late-join and streaming behavior.
- **Landscape Import:** Manual landscape import now creates TerraDyne chunks and resamples landscape data instead of leaving the editor commands stubbed out.

### Changed
- **Documentation:** Public docs now describe TerraDyne as a UE 5.7 CPU-authoritative runtime terrain system with optional hybrid render-target visuals.
- **Extensibility:** `ChunkClass` is now respected by the main chunk spawn paths used by loading and streaming.

## [1.2.0] - 2026-02-10
### Added
- **Hybrid Render-Target Support:** Added optional `UTextureRenderTarget2D` workflows for brush and material-driven visual effects.
- **Persistence System:** New Save/Load architecture (`UTerraDyneSaveGame`) for serializing terrain state to disk.
- **Showcase Demo:** Added `ATerraDyneOrchestrator` which runs a guided tour of features (Sculpt, Save/Load, LOD) when `BP_TerraDyneSceneSetup` is used.
- **Smart Material:** New `M_TerraDyne_Smart` master material with slope-based blending and procedural noise.

### Fixed
- **Collision Reliability:** Resolved physics "fall-through" issues by implementing collision throttling (`CollisionDebounceTime`) and ensuring `BlockAll` profiles.
- **LOD Performance:** Optimized distance-based culling to disable complex collision on distant chunks (>500m).
- **Import Pipeline:** Fixed `TerraDyneManager` import tools to correctly handle scale differences when importing from `ALandscapeProxy`.
- **Compilation:** Fixed missing includes (`APawn`, `Engine.h`) that caused build failures in strict compilation environments.

### Changed
- **Architecture:** Refactored `ATerraDyneChunk` to use a unified `UDynamicMeshComponent` for both visual and physical representation.
- **UI:** Updated `STerraDynePanel` to correctly display GPU/CPU backend status.
- **Packaging:** strict `FilterPlugin.ini` rules to ensure all Source and Config files are included for plugin distribution.
