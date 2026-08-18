# Chunk Streaming Design

**Date:** 2026-03-02
**Status:** Approved
**Approach:** Manager-Driven Ring Streamer (Approach A)

## Problem

TerraDyne spawns a fixed 3x3 grid of chunks at startup (~27 MB). There is no dynamic load/unload of distant chunks. The target is a 21x21 grid (441 chunks, ~2.1 km world) where only ~80 chunks are loaded at any time (~240 MB), streaming the rest on demand.

## Requirements

- **World scale:** 21x21 grid (configurable via `GridExtent`)
- **Generation:** FBM noise for unvisited chunks (existing pipeline)
- **Persistence:** Always persist dirty chunks to disk before unloading
- **Loading:** Async ring pattern around player, throttled to N ops/tick

## Configuration (UTerraDyneSettings)

| Setting | Default | Description |
|---------|---------|-------------|
| `ChunkLoadRadius` | 5 | In chunk units. Diamond-shaped scan. |
| `ChunkUnloadRadius` | 7 | Hysteresis gap of 2 prevents thrashing. |
| `MaxChunkOpsPerTick` | 2 | Spawn or teardown operations per streaming tick. |
| `StreamingTickRate` | 0.25s | Reuses existing LOD tick interval. |
| `ChunkSaveDir` | `TerraDyne/ChunkCache/` | Per-chunk save files relative to SaveGames. |
| `GridExtent` | 10 | Half-width: grid spans -10..+10 = 21x21. |

## Streaming State (ATerraDyneManager)

```
TSet<FIntPoint>         PendingLoadQueue
TSet<FIntPoint>         PendingUnloadQueue
TMap<FIntPoint, bool>   DirtyChunkMap
FIntPoint               LastStreamingCenter
```

`DirtyChunkMap` is set by `ApplyGlobalBrush()` for each affected chunk coordinate.

## Streaming Tick (every 0.25s)

1. **Compute player grid cell** from world position
2. **Early-out** if player hasn't moved cells (skip scan, process queues only)
3. **Scan for loads:** Diamond within `LoadRadius` centered on player cell. Skip out-of-grid, already loaded, already queued.
4. **Scan for unloads:** Iterate `ActiveChunkMap`. Any chunk with Chebyshev distance > `UnloadRadius` is queued.
5. **Process queues (throttled to `MaxChunkOpsPerTick`):**
   - Unloads first (free memory before allocating)
   - Unload priority: farthest from player first
   - Load priority: nearest to player first (Manhattan distance)
6. **Update LOD** (existing collision toggle)

## LoadOrSpawnChunk(FIntPoint Coord)

1. Check disk cache: `{ChunkSaveDir}/{Coord.X}_{Coord.Y}.chunk`
2. If file exists: read `FTerraDyneChunkData`, spawn actor, `LoadFromData(Data)`
3. If no file: spawn actor with FBM noise (existing `Initialize()` path)
4. Add to `ActiveChunkMap`, remove from `PendingLoadQueue`
5. Trigger `UpdateLOD(PlayerPos)`

## UnloadChunk(FIntPoint Coord)

1. If `DirtyChunkMap[Coord]`: serialize via `GetSerializedData()`, write to cache file with ZLib compression
2. Cancel pending grass regen (weak pointer already safe)
3. Remove from `ActiveChunkMap` and `DirtyChunkMap`
4. `chunk->Destroy()`

## Per-Chunk File Format

Reuses `FTerraDyneChunkData` serialized via `FMemoryWriter` + `FCompression::CompressMemory(NAME_Zlib)`. One file per coordinate, named `{X}_{Y}.chunk`.

## Persistence Integration

- **SaveWorld():** Unchanged — saves all currently-loaded chunks to a single save slot.
- **LoadWorld():** Loads chunks from save slot into `ActiveChunkMap`. Also writes loaded chunks to per-chunk cache files so streaming can find them. Clears `PendingLoadQueue` and `PendingUnloadQueue`.
- **Two systems coexist independently:** streaming uses per-chunk cache; SaveWorld uses single-slot saves.

## Multiplayer

- Streaming runs **server-only** (`HasAuthority()` guard)
- Spawned chunks replicate to clients via existing `bReplicates = true`
- Chunk destruction replicates automatically
- No change to Server_ApplyBrush / Multicast flow

## Edge Cases

| Case | Handling |
|------|----------|
| Player teleports | Full rescan, large queue fills, throttle prevents hitch |
| Brush hits unloaded chunk | `GetChunkAtCoord()` returns nullptr, brush skips silently |
| Unload during active stroke | Dirty flag was set by brush, chunk persists on unload |
| Save while streaming | Only active chunks saved; unloaded dirty chunks already on disk |
| Load while streaming | Clears all state, destroys all chunks, loads from slot, streaming resumes |
| Grid boundary | `GridExtent` clamp prevents out-of-bounds spawning |
| Grass on unload | `TWeakObjectPtr` + cancel token handles safely |

## Files to Modify

| File | Changes |
|------|---------|
| `Public/Settings/TerraDyneSettings.h` | 6 new streaming config properties |
| `Public/Core/TerraDyneManager.h` | Streaming state, queues, dirty map, new methods |
| `Private/Core/TerraDyneManager.cpp` | Streaming tick, LoadOrSpawnChunk, UnloadChunk, dirty tracking |
| `Private/Tests/TerraDyneStreamingTest.cpp` | New test: player movement simulation, load/unload verification |

## Memory Budget

- Per chunk: ~3 MB (buffers + mesh + texture)
- Load radius 5 (diamond): ~61 chunks loaded = ~183 MB
- With hysteresis unload at 7: peak ~113 chunks during movement = ~339 MB
- 21x21 grid total if all loaded: ~1.3 GB (never happens with streaming)
