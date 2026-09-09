# GRT Runtime World Framework

GRT now exposes a manager-owned runtime world layer on top of the existing landscape-to-runtime terrain pipeline.

## What Is Covered

- Authored world conversion:
  Landscape import, paint layer capture, placed foliage transfer, actor foliage transfer, save/load, and terrain replication remain the authored-world entry point.
- World population persistence:
  `AGRTManager` can register placed actors, place new persistent runtime actors, persist harvest/destroy/regrowth state, and restore that state through `SaveWorld` / `LoadWorld`.
- Procedural extension:
  `FGRTProceduralWorldSettings` enables deterministic seeded outskirts, biome overlays, runtime spawn rules, and optional infinite edge growth beyond the authored footprint.
- Gameplay hooks:
  Terrain, foliage, and population change delegates now broadcast from the manager. Navmesh dirty areas, AI spawn zone queries, biome queries, and build-permission checks are available through Blueprint-callable APIs.
- Designer workflow:
  `UGRTWorldPreset` packages biome overlays, population rules, AI zones, and build permissions. `AGRTSceneSetup` can apply a preset and choose a template (`Sandbox`, `Survival Framework`, `Authored World Conversion`).

## Core APIs

- `ApplyWorldPreset`
- `RegisterPersistentActor`
- `PlacePersistentActor`
- `PlacePersistentActorFromDescriptor`
- `HarvestPersistentPopulation`
- `SetPersistentPopulationDestroyed`
- `RefreshPopulationForLoadedChunks`
- `GetBiomeTagAtLocation`
- `CanBuildAtLocation`
- `GetAISpawnZonesAtLocation`
- `GetPCGSeedPointsForChunk`

## Data Types

- `FGRTPopulationDescriptor`
- `FGRTPersistentPopulationEntry`
- `FGRTPopulationSpawnRule`
- `FGRTBiomeOverlay`
- `FGRTProceduralWorldSettings`
- `FGRTProceduralChunkState`
- `FGRTAISpawnZone`
- `FGRTBuildPermissionZone`
- `FGRTPCGPoint`

## Typical Setup

1. Create a `UGRTWorldPreset` asset.
2. Configure procedural settings, biome overlays, population rules, AI zones, and build permissions in the preset.
3. Assign the preset to `AGRTManager.WorldPreset` or `AGRTSceneSetup.WorldPreset`.
4. Use `AGRTSceneSetup.DemoTemplate` to bootstrap a `Full Feature Showcase`, a sandbox, a survival-oriented runtime world, or an authored conversion level.
5. Query `GetPCGSeedPointsForChunk` from Blueprint or PCG-adjacent tooling when you need GRT-managed points for secondary generation.

## Automation Coverage

- `GRT.WorldFramework.PersistentPopulationSaveLoad`
- `GRT.WorldFramework.GameplayHooksAndProceduralMetadata`
