# Spline Modular Buildings

**Date:** 2026-08-14
**Status:** Draft — pending approval
**Approach:** Runtime building actor + separate terrain pad layer + custom runtime gizmo

## Problem

TerraDyne can sculpt terrain at runtime but has no way to author structures on it. We want to
place a building at runtime, define its footprint with an editable spline, set a floor count,
move it with a gizmo, and have the terrain under it flatten automatically.

## Requirements

- **Runtime**, in the existing `STerraDynePanel` — not editor-only.
- Footprint defined by a **spline**; points draggable, insertable, deletable.
- **Floor counter** — building grows vertically in whole storeys.
- **Move gizmo** for the whole building.
- Terrain under the footprint **flattens automatically**, with a blend skirt out to natural ground.
- Persists into `UTerraDyneSaveGame` and survives chunk streaming.

## Asset basis (`/Game/SD_Art/Industrial_Architecture`)

Mesh bounds queried from the live editor, not inferred from filenames:

| Asset | Size (uu) | Pivot / orientation |
|---|---|---|
| `SM_Wall_Panel_4x2M` | 200 W × 400 H × ~12 T | corner at base, runs **−Y** |
| `SM_Wall_Panel_4x4M` | 400 W × 400 H | same |
| `SM_Wall_Panel_4x8M` | 800 W × 400 H | same |
| `SM_Floor_4x4M` | 400 × 400, top face Z≈0 | corner |
| `SM_Beam_Corner_Left_4M` | 20 × 18 × 400 | corner, vertical post |
| `SM_Beam_4M` | 400 L × 30 H | corner |

Naming is `4x<width>M` where **4 = height**. Every wall panel is exactly one storey.

**Grid quantum: 400 uu.** Floor height 400 uu, panel widths 200/400/800 uu. No scaling needed.

4×4M variants: plain, `_Beams`, `_Beams_Door_Left/Center/Right`, `_Beams_Windows`,
`_Beams_Vent`, `_Beams_ElectricalBox`.

## Design

### Data model

```cpp
USTRUCT()
struct FTerraDyneBuildingData
{
    FGuid            BuildingId;
    FVector          Origin;          // world; the gizmo moves this
    FRotator         Rotation;
    TArray<FVector>  SplinePoints;    // LOCAL to Origin
    bool             bClosedLoop = true;
    int32            FloorCount = 2;
    FSoftObjectPath  KitAsset;
    float            PadTargetZ;      // world Z the footprint flattens to
    float            PadFalloff = 400.f;
    int32            DoorSegmentIndex = 0;
    uint32           RandomSeed = 0;
};
```

Spline points are stored **local to `Origin`**. Moving the building is then a one-field change,
and the flatten recompute needs no per-point fixup.

### Kit data asset

`UTerraDyneBuildingKit : UPrimaryDataAsset` — mesh set plus the pivot convention, so a kit with
different pivots can be dropped in without touching the generator:

```cpp
float ModuleWidth = 400.f;
float FloorHeight = 400.f;
TMap<int32, TSoftObjectPtr<UStaticMesh>> WallPanelsByWidth;  // 200 / 400 / 800
TSoftObjectPtr<UStaticMesh> WallPanel_Door, WallPanel_Window;
TSoftObjectPtr<UStaticMesh> CornerPost, FloorTile, FloorBeam, RoofTile;
FTransform MeshPivotCorrection;   // SD_Art kit: yaw +90 to map −Y run onto +X segment dir
```

Ship one preset: `DA_BuildingKit_Industrial`.

### Generation (`ATerraDyneBuildingActor::RegenerateBuilding`)

1. Walk the spline as a **polyline** (linear tangents — see Assumptions).
2. Per segment of length `L`: greedy-fill with 800/400/200 panels.
3. Per floor `f` in `0..FloorCount-1`: Z = `f * FloorHeight`; place panels along every segment.
4. Per spline point: `CornerPost`, one per floor.
5. Interior: slabs filled by **recursive subdivision** over a 400 grid. A cell whose full extent
   lies inside the footprint gets a 400 tile; otherwise it subdivides into 200s, then 100s, and a
   still-partial cell at the smallest size is left empty. Roof = same pass at
   `FloorCount * FloorHeight`.
6. Emit into pooled `UInstancedStaticMeshComponent`s, one per unique mesh.

> **Why subdivision, not a centre test.** The first implementation placed a 400 tile wherever the
> cell *centre* was inside. On any footprint edge that is not a whole number of modules that pushes
> slabs out through the walls — a 1500-long wall got tiles out to 1600, producing a visible ledge at
> every storey. Testing the whole cell and dropping to smaller kit tiles fixes it exactly: measured
> coverage on the L-shaped test footprint is 7x400² + 9x200² + 35x100² = 1,830,000 uu², against a
> true polygon area of 1,830,000 uu² — no gaps, no overlap.
>
> The cell test samples the four corners plus the centre, each **inset by 0.5 uu**. On a
> grid-aligned footprint the corners land exactly on the polygon edge, where a crossing-number test
> is ambiguous; without the inset the entire perimeter ring of tiles disappears.

A 10-segment 5-floor building is ~200 instances — one draw call per mesh. Regeneration is cheap
enough to run live on drag, debounced the same way `MarkLayerWeightsDirty` already debounces.

### Terrain flatten — pad as its own layer

**Key decision: building pads do NOT write into `SculptBuffer`.**

A new per-chunk `PadBuffer` joins the height sum:

```
Height = Base + Sculpt + Detail + Pad
```

`PadBuffer` is recomputed **from scratch from the building registry** whenever any building
changes. This means:

- Moving a building cannot leave a stale pad behind — there is nothing to undo.
- Overlapping buildings compose correctly and order-independently.
- A chunk streaming back in just asks the manager which buildings touch it and rebuilds its pad.
- Nothing extra goes in the save file — pads are derived state, recomputed on load.

Writing pads into `SculptBuffer` instead would require snapshotting and restoring the pre-flatten
region per building, which breaks as soon as two pads overlap.

New chunk entry point:

```cpp
void ATerraDyneChunk::ApplyPolygonFlatten(
    const TArray<FVector2D>& LocalPolygonXY,
    float TargetHeightWorld,
    float Falloff,
    float Strength);
```

Per grid vertex, `d` = signed distance to the polygon (negative inside):

| `d` | weight |
|---|---|
| `<= 0` | 1 |
| `0 .. Falloff` | `smoothstep` 1→0 |
| `> Falloff` | 0 |

`PadBuffer[i] = Lerp(0, Target - NaturalHeight, weight)`.

Cost: 16k verts × N edges per chunk at Resolution 128. One-shot on change, not per frame, and
only for chunks overlapping the padded bounds.

`PadTargetZ` defaults to the mean of `GetHeightAtLocation` sampled at the spline points, with a
manual override and a "re-level to terrain" action.

### Runtime gizmo

No engine gizmo exists at runtime, so this is from scratch. `ATerraDyneGizmoActor`: three axis
arrows on a dedicated collision channel, unlit translucent, scaled by camera distance to hold a
constant screen size.

Drag maths — the standard approach, avoiding the depth ambiguity of naive screen-delta mapping:

1. LMB down → deproject → trace gizmo channel → record which axis was hit.
2. Build a plane through the gizmo origin that contains the drag axis and most faces the camera.
3. Each frame, intersect the mouse ray with that plane; project the delta onto the axis.
4. Release → commit → Server RPC → pad recompute.

**Spline point handles**: sphere meshes at each point, same picking path. Drag is constrained to
XY (Z follows terrain). Ctrl+click a segment midpoint inserts a point; Delete removes the
selected one, guarded at a minimum of 3 for a closed loop.

Selection: click a building's ISM to select and spawn the gizmo; click empty space to deselect.

### Networking

Mirrors the existing "replicate the *edit*, not the geometry" pattern. New Server RPCs on
`ATerraDyneEditController`:

- `Server_SpawnBuilding(FVector Location, FSoftObjectPath Kit)`
- `Server_UpdateBuilding(FGuid Id, FTerraDyneBuildingData Data)` — sent on drag **release**, not per frame
- `Server_DeleteBuilding(FGuid Id)`

The building actor replicates `FTerraDyneBuildingData` with a `ReplicatedUsing` that regenerates
and refreshes pads. During a local drag the client regenerates locally for responsiveness.

### Persistence

`UTerraDyneSaveGame` gains `TArray<FTerraDyneBuildingData> Buildings`. `BuildSaveGameInstance()`
collects from the manager registry; `ApplyLoadedSaveGame` respawns actors and recomputes pads.

### UI — new BUILDINGS section in `STerraDynePanel`

```
▼ BUILDINGS
  [ Place Building ]              armed; next terrain click drops it
  Kit:      [ Industrial ▾ ]
  ──── selected ────
  Floors:   [ − ]  3  [ + ]       the floor counter
  Height:   12.0 m
  Points:   6
  Mode:     [ Move ] [ Edit Points ] [ Add Point ]
  Pad falloff: [====|----] 400
  [ Re-level to terrain ]
  [ Delete Building ]
```

New `ETerraDyneToolMode` entries: `PlaceBuilding`, `MoveBuilding`, `EditSpline`. The panel's
existing `SetToolMode` / `GetToolModeColor` handle the active-button highlight unchanged.

## Files

**New** — `Source/TerraDyne/{Public,Private}/Building/`:

| File | Purpose |
|---|---|
| `TerraDyneBuildingTypes.h` | `FTerraDyneBuildingData`, enums |
| `TerraDyneBuildingKit.h/.cpp` | kit data asset |
| `TerraDyneBuildingActor.h/.cpp` | spline + ISM + generator |
| `TerraDyneGizmoActor.h/.cpp` | runtime translate gizmo |

**Modified:**

| File | Change |
|---|---|
| `World/TerraDyneTileData.h` | `FlattenPolygon` brush mode |
| `World/TerraDyneChunk.h/.cpp` | `PadBuffer`, `ApplyPolygonFlatten`, height sum |
| `Core/TerraDyneManager.h/.cpp` | building registry, pad recompute, streaming re-assert |
| `Core/TerraDyneSaveGame.h` | `Buildings` array |
| `Core/TerraDyneEditController.h/.cpp` | tool modes, gizmo drag, point picking, RPCs |
| `UI/TerraDyneToolWidget.h` | new tool modes |
| `UI/STerraDynePanel.h/.cpp` | BUILDINGS section |

## Phasing

| # | Phase | Outcome |
|---|---|---|
| 1 | Static generation | **DONE** — kit + actor + generator, verified against a rectangle and a concave L with non-module edge lengths. |
| 2 | Terrain flatten | **DONE** — pad layer, polygon SDF, manager recompute, streaming re-assert. Verified in PIE against live terrain (see Phase 2 results). |
| 3 | Placement + panel UI | **DONE** — BUILDINGS section, place-on-click, select, floors ±, re-level, delete. Verified by placing three buildings by click in PIE. |
| 4 | Runtime gizmo | Translate gizmo + selection |
| 5 | Spline point editing | Handles, insert, delete |
| 6 | Persistence + networking | Save/load + Server RPCs |

## Phase 2 results (measured in PIE, 61 live chunks, Resolution 128, ZScale 8000)

Building with the default 1200x800 footprint, `PadFalloff` 400, on terrain naturally varying
1374-2243 uu across the footprint.

| Property | Result |
|---|---|
| Pad target = mean natural height at footprint corners | 1733.47 vs 1733.47 — exact |
| Ground inside the footprint | flat at 1733.47 across every sample |
| Skirt blend, 600 -> 1000 | 1737 -> 1808 -> 1955 -> 2076, monotonic |
| Ground beyond the skirt | delta **+0.00** |
| **Move the building** — old site | restored bit-exact, worst delta **0.0000** |
| **Move the building** — new site | flattened to the new target exactly |
| **Sculpt under the building** (6 raise dabs @ 2500) | pad holds, worst deviation **0.0004 uu**; same sculpt visible outside the skirt |
| **Delete the building** | natural terrain restored, worst delta **0.0002** |
| **Save + reload world** | pad preserved exactly, worst delta **0.0000** (a doubled pad would be ~2x off) |

The move and sculpt rows are the ones that justify the design. A single delta buffer would have
left a ghost pad behind on the move, and would have let the sculpt push the floor up through the
building.

### Automation suite after the refactor

Run headless against a clean rebuild (`-NullRHI`, editor closed): **16 passed, 2 failed**.

Everything covering the changed height path passed — `Functional.Deformation`, `Undo.RoundTrip`,
`Undo.HistoryTrim`, `Rendering.SerializationSanitizesResolution`, `Streaming.RingLoadUnload`,
`Migration.SaveLoadRoundTrip`, `WorldFramework.*`.

The two failures were **pre-existing and unrelated** — both failed on `Failed to find object`
against `/Game/TerraDyne/Materials/...`, i.e. the *host project's* Content folder. The plugin's
materials live under `Plugins/TerraDyne-main/Content/Materials/...`, which mounts as
`/TerraDyne/Materials/...`. There is no `Content/TerraDyne/` folder at all, so those loads had been
silently returning null.

**Fixed** (7 references, all to assets the plugin ships): `TerraDyneSettings.cpp` x2,
`TerraDyneOrchestrator.cpp` x2, `TerraDyneMigrationTests.cpp` x3 — `/Game/TerraDyne/` ->
`/TerraDyne/`. Fixing only the tests was not possible: `ShowcaseMaterialSelection` asserts on
`Manager->MasterMaterial->GetPathName()`, so it can only pass if the orchestrator genuinely loads
the asset.

Deliberately left alone: `/Game/M_Metal_Terrain` (project-supplied, not plugin content),
`/Game/TerraDyne/Blueprints/WBP_TerraDyneHUD` (exists under neither prefix, and has a working
fallback to `UTerraDyneToolWidget`), `/Game/TerraDyne/BakedData` (an *output* path — writing baked
assets into the project rather than the plugin is correct).

Result after the fix: **16 passed, 1 failed** (up from 15). `Demo.ShowcaseMaterialSelection` now
passes.

### TerraDyne.Rendering.MaterialBindings — fixed

The path fix un-masked a second, deeper failure that had been hiding behind the first:

```
Expected 'Height RT created' to be not null.       TerraDyneMigrationTests.cpp:200
HeightMap bound to chunk RT: values are not equal. TerraDyneMigrationTests.cpp:210
```

A stale test against a deliberate code change. `ATerraDyneChunk::Initialize()` no longer calls
`SetupGPU()` — see the comment there: it built two render targets plus a synchronous
`FlushRenderingCommands()` + readback to serve `ApplyBrushGPU`, which nothing invokes. So `HeightRT`
is permanently null and the material binds `CPUHeightTexture` instead.

The assertions now follow `IsUsingGPU()` and check whichever texture is live. The test's own
diagnostic was also wrong and has been rewritten: it blamed the automation RHI, but this is not an
RHI limitation — the GPU path is unreachable in every configuration, editor included.

**Suite is now 17 passed, 0 failed.**

### Still outstanding: the unreachable GPU path

Verified by call-site search, not by trusting the comments:

| Symbol | Lines | Call sites |
|---|---|---|
| `SetupGPU()` | 58 | **0** |
| `ApplyBrushGPU()` | 35 | **0** |
| `UpdateRenderTargetFromHeightmap()` | 51 | only from `bUseGPU` branches |
| `ReadbackRenderTarget()` | 18 | only from `bUseGPU` branches |

`bUseGPU = true` appears exactly once, at `TerraDyneChunk.cpp:788`, *inside* `SetupGPU()`. Since
nothing calls it, `bUseGPU` is provably always false, `HeightRT`/`HeightRT_Swap` are always null,
and four branch sites are dead or vacuous.

Deleting it (~162 lines plus 37 `HeightRT` references) is the better end state, but it is deliberately
deferred: it should be its own change, not folded into feature work on the same files. Note also
that `GetSerializedData()` documents that reconnecting the readback naively would overwrite
sculpted CPU data and silently revert terrain — so this is a half-finished path with a known
correctness trap, not a working mode waiting to be switched on.

### Height composition was centralised

The same `Base + Sculpt + Detail` sum was written out by hand in **seven** places across
`TerraDyneChunk.cpp`, `TerraDyneManager.cpp` and `TerraDyneToolWidget.cpp`. Adding a fourth layer
to seven independent copies is how one gets missed, so they were all replaced with
`ATerraDyneChunk::RecomputeHeightFromLayers()`. The only remaining hand-written sum is the one
inside that function.

Two bugs found by review before testing, both already fixed:

1. **Pad height drifted with streaming.** The auto height averages natural ground under the
   footprint corners, but can only read *loaded* chunks — so a building spanning two chunks got one
   answer, then a different one when the second chunk arrived, leaving a step where they met. The
   sample is now taken once and cached until the building actually changes; streaming never
   invalidates it.
2. **A full buffer scan per chunk stream-in.** `HasBuildingPad()` swept Resolution^2 floats, and the
   manager calls it for every chunk on every load — in a world with no buildings that is 16k floats
   read to answer "no". Now a tracked flag.

## Phase 3 results

Panel section added to `STerraDynePanel` alongside SCULPTING: Place / Select tool modes, selected
building readout, floor counter, Re-level, Delete.

Building clicks are **one-shot**. The sculpt path latches `bIsClicking` and re-applies every tick
while held — right for a brush, and it would carpet the terrain with actors here.
`OnLeftClickStart` branches on the tool mode and the building path never sets that flag;
`PerformToolAction` also returns early on a building mode, covering a tool switch made mid-drag.

Also fixed a **pre-existing input leak**: the panel's `OnMouseButtonDown` returned `Unhandled` for
everything outside the 50px drag header, so clicks on panel background — the gaps between buttons,
the surrounding border — fell through to the world and sculpted the terrain behind the UI. It now
absorbs clicks landing on its box. The widget's own slot spans the whole viewport, so the test has
to run against the inner `SBox`'s geometry, not the handler's.

No networking: `IsInstructor()` is `IsLocalController() && HasAuthority()`, so the panel only
exists on the host and placement always runs on the authority. Replicating placed buildings to
other machines remains phase 6.

## Terrain material: three stacked faults

Surfaced while testing phase 3. Worth recording because each one masked the next.

1. **Plugin content mount.** Every material path read `/Game/TerraDyne/...` — the *host project's*
   Content folder — for assets that ship in the plugin and mount at `/TerraDyne/...`. Those loads
   returned null, `SetMaterial(nullptr)` fell back to `/Engine/BasicShapes/BasicShapeMaterial`, and
   the terrain had been rendering plain engine grey. Fixing the mount switched the real material on
   for the first time, which exposed the next two.

2. **`WeightMap` never bound.** `ApplyTerrainMaterialParameters` set `HeightMap` and the scalars but
   skipped `WeightMap`. Every terrain material declares it and defaults it to
   `/Engine/EngineResources/DefaultTexture` — the engine **checkerboard**. Unbound therefore does
   not mean "no weights", it multiplies the ground blend by a checker. The layer split comes from
   vertex colours now (`ApplyLayerWeightsToMesh`), so neutral white is the identity.

3. **Defaults pointed at the bare master.** `M_TerraDyne_Master` leaves all five ground textures
   (`Grass`/`Dirt`/`Rock`/`Stones`/`Snow`) null, and a null texture parameter also samples as
   checkerboard — so the fallback could never render regardless of fix 2. `MI_TerraDyne_Terrain` is
   the same material with those textures assigned.

Neither 2 nor 3 is sufficient alone: the instance supplies the ground textures, the code supplies
the neutral `WeightMap`, and the instance leaves `WeightMap` unoverridden so it inherits the
master's checker default either way.

Verified on a cold PIE start with no runtime patching. Suite 17/17.

## Assumptions

- **Spline is a polyline** (linear tangents). The kit is flat panels; curved segments would need
  either sub-400uu pieces or visible gaps. Revisit only if curved buildings are wanted.
- Optional **400 uu grid snap** on point placement, to keep segment remainders at zero.
- One building = one closed loop. Courtyards / L-shapes come from the polygon, not multiple loops.

## Risks

1. **Segment remainders** on arbitrary spline lengths — mitigated by grid snap; worst case a
   200 uu panel plus a small overlap at one end of each segment.
2. **Runtime gizmo is the largest single phase** — no engine support, all picking and drag maths
   is ours.
3. **Pad recompute cost** if many buildings sit in one chunk — bounded by only recomputing chunks
   in the changed building's padded bounds.
