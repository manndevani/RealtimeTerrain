# RealtimeTerrain (Source/RealtimeTerrain) — Code Reference

> **Architecture Note**: The runtime road system that used to live here (road/junction actors, the
> builder component, the curve-graph/solver/mesh/spiral/profile/pier/style/XODR libraries, the edit
> overlay, and their ~20 test files) was deleted on 2026-08-31 — see [docs/DEVELOPMENT_HISTORY.md](../../docs/DEVELOPMENT_HISTORY.md); it
> survives in git history only. `Source/RealtimeTerrain/` is now a thin project game module: a game
> mode and an edit controller over the `GRT` plugin.

Architecture summary, conventions, and current project status live in [README.md](../../README.md),
[docs/FEATURES.md](../../docs/FEATURES.md), [docs/DEVELOPMENT_HISTORY.md](../../docs/DEVELOPMENT_HISTORY.md)
and [CLAUDE.md](../../CLAUDE.md) — this file documents the game module.

---

## C++ files

### Source/RealtimeTerrain/RealtimeTerrain.cpp
**Class/struct(s):** none (module implementation only)
**Purpose:** Registers `RealtimeTerrain` as a primary game module with the Unreal module system, using the default module implementation since the module needs no custom startup/shutdown logic.
**Key API / responsibilities:**
- `IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, RealtimeTerrain, "RealtimeTerrain")` — the sole line of code.
**Dependencies:** `RealtimeTerrain.h`, `Modules/ModuleManager.h`.

### Source/RealtimeTerrain/RealtimeTerrain.h
**Class/struct(s):** none (only `#pragma once` and `CoreMinimal.h`)
**Purpose:** Standard per-module public header stub.

### Source/RealtimeTerrain/Public/RealtimeTerrainEditGameMode.h
**Class/struct(s):** `ARealtimeTerrainEditGameMode` (UCLASS, extends `AGRTEditGameMode` from the GRT plugin)
**Purpose:** Exists so `PlayerControllerClass` can point at the project's own controller class, which lives in this module rather than the plugin. Pawn class and the multi-player spread-out spawn handling are inherited unchanged.
**Key API / responsibilities:**
- `ARealtimeTerrainEditGameMode()` — declared constructor; the only member the subclass adds.
**Dependencies:** `Core/GRTEditGameMode.h`, `RealtimeTerrainEditGameMode.generated.h`.

### Source/RealtimeTerrain/Private/RealtimeTerrainEditGameMode.cpp
**Class/struct(s):** `ARealtimeTerrainEditGameMode` (constructor definition)
**Key API / responsibilities:**
- `ARealtimeTerrainEditGameMode::ARealtimeTerrainEditGameMode()` — sets `PlayerControllerClass = ARealtimeTerrainEditController::StaticClass();` and nothing else.
**Dependencies:** `RealtimeTerrainEditGameMode.h`, `RealtimeTerrainEditController.h`.

### Source/RealtimeTerrain/Public/RealtimeTerrainEditController.h
**Class/struct(s):** `ARealtimeTerrainEditController` (UCLASS, extends `AGRTEditController`)
**Purpose:** The project's player controller. Adds nothing yet — every terrain, building, model, and OSM editing path is inherited from `AGRTEditController` — but gives the project a class of its own to extend without touching the plugin.
**Key API / responsibilities:** `ARealtimeTerrainEditController() = default;`
**Dependencies:** `Core/GRTEditController.h`.
**Notable implementation details:** Renamed from `ARealtimeRoadEditController` on 2026-09-10, once the road hooks it used to override were gone. `Config/DefaultEngine.ini` carries `+ClassRedirects=(OldName="/Script/RealtimeTerrain.RealtimeRoadEditController",NewName="/Script/RealtimeTerrain.RealtimeTerrainEditController")` so anything saved against the old name still loads.

### Source/RealtimeTerrain/Private/RealtimeTerrainEditController.cpp
**Class/struct(s):** `ARealtimeTerrainEditController` (implementation — include only).

---

## Content assets

`Content/Roads/` (`BP_RoadSpline`, `BP_RoadJunction`, `M_Proto_Asphalt`/`Curb`/`Footpath`/`RoughRoad`)
and `Content/Materials/M_RoadHolographic` are left over from the removed road system. The two
Blueprints' parent classes (`/Script/RealtimeTerrain.RealtimeRoadSplineActor` /
`RealtimeRoadJunctionActor`) no longer exist, so they cannot load, and no code references any of
these assets. They are candidates for deletion.
