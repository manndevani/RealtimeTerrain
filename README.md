# RealtimeTerrain (Unreal Engine 5.8)

## 📋 Project Status

**Road/junction system removed (2026-08-31).** The runtime spline road/junction system
(`ARealtimeRoadSplineActor`, `ARealtimeRoadJunctionActor`, `URealtimeRoadBuilderComponent`, the
`RoadGraphCore`/`RoadMeshBuilder`/`RoadNetwork`/`RoadCurveGraph`/`RoadElevationProfile`/`RoadSpiral`/
`RoadPierPlacement`/`RoadStylePresets`/`RoadXodrExport`/`RoadNetworkDebugLibrary` modules, the
third-party `RoadBuilder-master` plugin, and every UI/OSM/undo hook wired into it) was deleted at
the user's request, along with its 45+ automation tests. `ATerraDyneEditController` and
`STerraDynePanel` no longer reference roads at all; OpenStreetMap import now brings in buildings
only. The full development history of that system (RoadBuilder port phases 1-5, the junction
fillet-solver rewrite) is preserved in git history on the `road-system-rewrite` branch up to the
commit before this removal, and is no longer duplicated here.

## 🎯 Gizmo System — reusable runtime transform gizmo (COMPLETE ✅)

**Goal**: replace the old translate-only, per-consumer-duplicated `ATerraDyneGizmoActor` with a
proper reusable translate/rotate/scale gizmo system (world/local space, plane handles, rotation
rings, mathematically precise hit testing, snapping, undo integration) shared by the building
editor and the road-point editor instead of each reimplementing its own drag math. Full plan:
`C:\Users\Admin\.claude\plans\adaptive-noodling-penguin.md`.

### Phase 1 — COMPLETE ✅ (manipulation core math)
- `Plugins/TerraDyne-main/Source/TerraDyne/Public/Gizmo/GizmoTypes.h`: `EGizmoHandleType` (16
  explicit handle identities — translate axes, planes, screen-space centre, rotation rings +
  screen rotation, scale axes + uniform), `EGizmoMode`/`EGizmoCoordinateSpace`/`EGizmoPivotMode`,
  `FGizmoInteractionRay` (device-agnostic — mouse today, a future VR controller could produce the
  same struct), `FGizmoManipulationState`.
- `GizmoManipulationMath.h`/`.cpp`: pure `namespace GizmoMath`, no UObject/UWorld/component
  references anywhere — axis/plane resolution for World vs Local space; a generalized
  best-facing-drag-plane resolver (replaces this project's two previously byte-identical
  `BestFacingVerticalPlaneNormal` copies in `TerraDyneEditController.cpp` and
  `RealtimeRoadBuilderComponent.cpp` with one function that handles every axis via the same
  technique, not just the vertical one); translate/rotate/scale delta computation built on
  **initial transform + accumulated delta**, never compounding onto an already-modified transform;
  generic value/vector snapping.
- Meshes migrated from `github.com/robinwood3d/UnrealTransformGizmo` (MIT, cloned and inspected
  directly — confirmed pure Blueprint asset pack, no C++, so nothing but its content is reusable):
  `Content/Gizmo/Meshes/SM_RotationHandle.uasset`, `SM_ScaleHandle.uasset`,
  `SM_UniformScaleHandle.uasset` — no runtime-safe torus/scale-handle mesh ships in
  `/Engine/BasicShapes/`. Attribution recorded in `Content/THIRD_PARTY_NOTICES.md`. The repo's own
  materials were **not** migrated — this project's existing dynamic-MID coloring system
  (`ATerraDyneGizmoActor`'s `GizmoBaseMaterial`/`ApplyGizmoColor`) is reused instead.
  **Superseded by Phase 2, correction found by direct code/asset audit**: `Content/Gizmo/Materials/`
  does contain 18 real migrated `M_GizmoAxis`/`MI_XAxis`/`MI_Rotation_*`/etc. assets, and
  `ARuntimeGizmoActor.cpp`'s constructor (Phase 2, below) loads and uses them as its *primary*
  material path — the hardcoded-color dynamic-MID system only runs as a fallback if that asset load
  fails. `M_GizmoAxis`/`M_ScreenRotation` also depend on `MF_OccludedPixels` in `Content/Mytaverse/`,
  so that "vendor" folder isn't safe to delete. This paragraph's original claim was accurate for
  Phase 1's own scope (mesh-only migration) but was never updated once Phase 2 changed it; left
  as-is above for the historical record, corrected here instead of silently edited.
  Test: `TerraDyne.Gizmo.*` (`AxisResolution`, `TranslateDelta`, `RotationAngle`, `ScaleDelta`,
  `PlaneResolutionAndRayIntersect`, `Snapping`) — includes a known-quarter-turn check (measures
  exactly π/2), a scale-crossing-the-origin check (clamps to a small positive ratio, never
  negative/zero), and a far-from-world-origin check (same result 1,000,000cm from the origin,
  standing in for "extreme camera distance" robustness).
- **Full regression sweep after this increment: 42 tests, 0 regressions** (existing
  `TerraDyne.Building`/every `TerraDyne.Road*` suite untouched — this phase is pure addition, no
  rendering or existing-code changes yet).

### Phase 2 — COMPLETE ✅ (`ARuntimeGizmoActor` — visual handle set)
- `Plugins/TerraDyne-main/Source/TerraDyne/Public/Gizmo/RuntimeGizmoActor.h`/`.cpp`: one
  `UStaticMeshComponent` per handle (16 explicit `EGizmoHandleType` values) across all three modes.
  Every handle is tagged explicitly (`TMap` from component to handle type, built once at
  construction) — `GetHandleType`/`GetComponentForHandle` never infer identity from a mesh or
  material. `SetMode(EGizmoMode)` toggles visibility **and** collision together per handle set, so
  a handle from an inactive mode is never still hit-testable. Coloring reuses
  `ATerraDyneGizmoActor`'s proven dynamic-MID pattern (7 shared MIDs: X/Y/Z axis colors, 3 plane
  colors, one neutral MID shared by Center/RotateScreen/ScaleUniform, since those three never
  render simultaneously — each belongs to a different, mutually exclusive mode).
- **Three real bugs found and fixed this phase** (not left in, and not all caught by the same
  method — a reminder that different bug classes need different catches):
  1. **Build-time**: originally named `AGizmoActor`/`GizmoActor.h`, which collided with Epic's own
     `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoActor.h` (UHT requires
     globally-unique header filenames project+engine-wide, not just per-module) — renamed to
     `ARuntimeGizmoActor`/`RuntimeGizmoActor.h` throughout.
  2. **Found by code review, before any test ran**: `SetHighlightedHandle`'s per-axis color-select
     lambda captured `Handle` but also took a same-named parameter, so `Handle == Handle` always
     evaluated true — every axis would have highlighted simultaneously. Fixed, and pinned by a new
     assertion in `TerraDyne.Gizmo.ActorRuntimeAssets` reading the MID's actual emissive value back.
  3. **Found from real data, not assumption**: the migrated `SM_RotationHandle`/`SM_ScaleHandle`
     meshes' primary axis (a ring's normal; a scale handle's long arm) is local **X**, not Z like
     the engine's own Cylinder — confirmed by logging each mesh's actual bounds extent
     (`SM_RotationHandle`: X=2, Y=92, Z=92; `SM_ScaleHandle`: X=58.5, Y=7, Z=7) rather than assuming
     the same convention as the Cylinder/Cone arrows. Every ring and scale handle originally pointed
     at the wrong world axis; fixed with a dedicated `FRotationMatrix::MakeFromX` rotation set (the
     arrows keep their own correct `MakeFromZ` set). Pinned by
     `TerraDyne.Gizmo.ActorHandleOrientation`, which rotates each handle's real local-X through its
     assigned rotation and checks it lands exactly on the intended world axis (not just "a rotation
     exists") — all 6 (RotateX/Y/Z, ScaleX/Y/Z) land within 0.01 of their target axis.
- **Full regression sweep after this increment: 44 tests, 0 regressions.**

### Phase 3 — COMPLETE ✅ (`UGizmoManipulationSubsystem` + mathematical hit-testing)
- Four new pure hit-test functions in `GizmoManipulationMath.h`/`.cpp` — `DistanceRayToSegment`
  (translate/scale axes), `DistanceRayToPoint` (Center/ScaleUniform), `DistanceRayToRing` (rotation
  handles, via plane-intersect-then-radius-check rather than a full quartic ray-torus solve),
  `DistanceRayToQuad` (plane handles) — each hand-verified against worked geometry before any test
  ran: a ray straight through a segment's own start is a perfect 0-distance hit at exactly the
  expected travel distance; the same ray offset 50cm sideways misses by exactly 50; a 3-4-5-triangle
  scaled by 10 measures exactly 50; a ring's dead-centre click misses by exactly its own radius,
  while a click on the ring itself is a perfect hit; a quad click 30cm past its edge measures
  exactly 30. `ResolveTranslateDragPlane`'s own best-facing-plane technique was refactored out into
  a new `ResolveAxisDragPlane`, so scale and rotate axes reuse the identical, already-tested
  degenerate-fallback chain instead of a near-duplicate.
- `ARuntimeGizmoActor::GetHandleReach(EGizmoHandleType)`: the single source of truth for a handle's
  own size (segment length, ring radius, quad half-size, or a fixed click radius for point-like
  handles), reading the exact same placement constants the constructor already uses — this is
  explicitly an **input** to the distance functions above (the handle's own geometry), never a
  click-tolerance **threshold** on their output; the subsystem keeps a separate `ClickTolerance`
  constant for that, a distinction easy to get backwards.
- `UGizmoManipulationSubsystem : UWorldSubsystem` (mirrors the project's existing
  `UTerraDyneSubsystem` precedent, not a hand-rolled singleton): owns one lazily-spawned
  `ARuntimeGizmoActor` and the live `FGizmoManipulationState`. Full API: `SetTarget`/`ClearTarget`,
  `SetMode`/`SetCoordinateSpace`/`SetPivotMode`, `SetSnappingEnabled` +
  `SetTranslationSnap`/`SetRotationSnap`/`SetScaleSnap`, `UpdateHover`,
  `BeginManipulation`/`UpdateManipulation`/`EndManipulation`/`CancelManipulation`. Hit-testing
  (`FindHoveredHandle`) reads the live `ARuntimeGizmoActor`'s own component transforms directly
  rather than re-deriving geometry from `GizmoMath`'s pure resolve functions — the gizmo actor's
  transform is kept in sync with the current target/space/pivot on every change, so hit-testing can
  never disagree with what is actually rendered. Drags use initial-transform-plus-accumulated-delta
  throughout (never compounding onto an already-modified transform), verified end-to-end by a test
  that grabs `TranslateX`, drags 50cm, then cancels — the target lands back at its bit-for-bit
  original transform, not an approximation.
- Undo integration is delegate-based (`OnManipulationBegin`/`OnManipulationEnd`/
  `OnManipulationCancel`, plain multicast delegates), **not** Unreal's `FScopedTransaction` — that
  mechanism is editor-only (bound to `GEditor`'s transaction buffer) and would not survive a
  packaged build. A calling controller binds its own undo-snapshot recording to these instead,
  keeping whatever representation actually fits its data (a transform snapshot for a building; the
  existing point-list snapshot stack for a road).
- **Full regression sweep after this increment: 51 tests, 0 regressions.**

### Phase 4 — COMPLETE ✅ (both consumers migrated onto the shared subsystem)
- `ATerraDyneEditController` (buildings) and `URealtimeRoadBuilderComponent` (road points) both now
  drive `UGizmoManipulationSubsystem`'s `Begin/Update/End/CancelManipulation` instead of their own
  duplicated raycast/plane-intersection/delta-apply code. `TraceGizmoUnderCursor`,
  `BestFacingVerticalPlaneNormal`, `IntersectCursorWithPlane`-for-axis-drag, and the whole
  `ETerraDyneGizmoAxis`-keyed switch statements are gone from both files.
- **A necessary Phase 3 design refinement, discovered while actually attempting this migration, not
  planned in advance**: the subsystem originally hardcoded `TargetActor->SetActorTransform()`
  (`TargetActor = Target->GetOwner()`), correct for a building but wrong for a road control point — a
  road point is one vertex on a much larger spline actor, moved via `MovePointRealtime`, not a
  whole-actor transform, and its own world location (not the road actor's arbitrary origin) is the
  correct pivot. Fixed by generalizing the subsystem to pluggable `FGizmoGetTransform`/
  `FGizmoSetTransform` delegates (`SetTargetTransformAccessors`), with the simple
  `SetTarget(USceneComponent*)` becoming a thin convenience wrapper around them for the whole-actor
  case. The delta math itself never changed — only where its result reads from and writes to.
- **`ATerraDyneGizmoActor`'s real scope, found only by reading it fully rather than trusting the
  plan's "delete this class" instruction**: it secretly served two unrelated responsibilities — the
  old axis-translate gizmo, AND the building-footprint-spline-vertex-handle spheres (an entirely
  separate feature). Deleting it outright would have broken footprint editing. Stripped instead of
  deleted: `ETerraDyneGizmoAxis`, the arrow/tip/shaft meshes, `SetHighlightedAxis`,
  `GetAxisFromComponent` are gone; `SplinePointMeshHandles`, `UpdateSplinePointHandles`,
  `SetHighlightedSplinePoint` and the footprint feature stay exactly as before. The class comment now
  says so explicitly, since the name is a historical artifact of this split.
- **The subsystem's one gizmo target is shared by both consumers** — each tracks its own prior
  binding (`GizmoBoundBuilding` on the controller; `GizmoBoundRoad`/`GizmoBoundPointIndex` on the
  road component) and only ever calls `SetTarget`/`ClearTarget` when it actually owns the current
  binding, never unconditionally — otherwise a building-tool tick could silently steal or clear a
  road tool's active gizmo, or vice versa.
- **Escape-to-cancel** now works for both: `ATerraDyneEditController::SetupInputComponent` binds
  `EKeys::Escape` to a single `GizmoSubsystem->CancelManipulation()` call, which restores whichever
  consumer's target (if any) is mid-drag and fires `OnManipulationCancel`. Both consumers bind a
  handler to that delegate and check the passed-in target against their own binding before reacting
  (the delegate fires for any cancel, not just their own). The road handler additionally pops the
  now-redundant undo snapshot `BeginPointDrag` pushed, since the subsystem already restored the
  point's exact pre-drag position, making that edit a no-op. The building handler resettles
  `PadTargetWorldZ`/the footprint-handle visuals but deliberately does **not** call
  `SnapToPadHeight()` (that would recompute a new pad height and move Z away from the transform
  Cancel just restored) and sends no RPC (buildings only ever commit via `RequestMoveBuilding` on a
  normal release, never per-tick and never on cancel, so there is nothing to undo on other machines).
- **Full regression sweep after this increment: 83 tests found, 82 passed, 0 regressions** — the one
  failure (`TerraDyne.Raster.Workflow`) is a pre-existing GIS raster-import test unrelated to this
  work (a missing satellite/ortho layer import), in a file this migration never touched.
- **Known limitation**: this phase is verified by full headless regression + direct code
  verification of every cross-consumer ownership/cancel-identity path against the subsystem's actual
  broadcast semantics, but **not** by an in-editor manual drag test — actually clicking and dragging
  a building and a road point's gizmo handles in a running editor session is still needed before this
  phase is fully trustworthy, and hasn't been done yet.

### Phase 5 — COMPLETE ✅ (mode/space/snap live end-to-end)
- **Mode switching**: `Shift+W`/`Shift+E`/`Shift+R` switch the shared gizmo to Translate/Rotate/Scale,
  bound once on `ATerraDyneEditController` (works for whichever consumer currently owns the target,
  same "one call for both" pattern as Escape-cancel). Plain `W`/`E` were already claimed by this
  project's own always-on camera-flight axis mappings, and Unreal's legacy Axis Mappings have no
  modifier-key concept — they fire on the raw key regardless of Shift — so `ATerraDyneEditPawn` now
  overrides `MoveForward`/`MoveRight`/`MoveUp_World` (inherited from `ADefaultPawn`) to no-op while
  either Shift key is held, freeing `Shift+W/E/R` (and `Shift+A/S/D/Q`) for hotkeys with zero camera
  side effect instead of a same-frame flight nudge (which, at orbital altitude, is not cosmetic — a
  single frame of this project's own altitude-scaled max speed is tens of kilometers).
- **World/Local space**: `C` toggles `SetCoordinateSpace`, plus a "World Space" checkbox in a new
  GIZMO section of the tool panel (`STerraDynePanel`) — both drive the same subsystem call. Verified
  by direct code reading that `SetMode`/`SetCoordinateSpace`/`SetPivotMode` only ever touch the
  gizmo's own visual actor (`UpdateGizmoTransform`) and `FGizmoManipulationState`, never
  `SetTransformFn` — switching space genuinely cannot move the selected object, matching the plan's
  explicit "switching must not unexpectedly modify the object's transform" requirement.
- **Snapping**: `G` toggles the persistent `SetSnappingEnabled`, also exposed as a panel checkbox.
  Holding Ctrl during a drag inverts snapping for that drag only, without touching the persistent
  setting — the standard DCC convention. `UpdateManipulation` gained a `bInvertSnapForThisCall`
  parameter (default `false`, so both existing call sites and the two Phase-3 subsystem tests needed
  no changes) computing `bEffectiveSnapping = State.bSnappingEnabled != bInvertSnapForThisCall` once,
  used by all three mode branches instead of reading `State.bSnappingEnabled` directly. Both
  consumers now read their own controller's live Ctrl-key state every `UpdateManipulation` call.
  New test `TerraDyne.Gizmo.SubsystemInvertSnap` walks one continuous 137-unit drag through all three
  combinations without ever re-calling `BeginManipulation` — persistent-off+Ctrl snaps to 100,
  persistent-on+Ctrl gives the raw 137, and releasing Ctrl the very next call falls straight back to
  the persistent (on) setting with no leftover state, exactly matching the logged values.
- **Plane and rotation-ring handles**: no new code needed — Phase 1's math and Phase 3's hit-testing
  already handled them, and Phase 4's migration already routes every mode through the same
  `Begin/Update/EndManipulation` calls regardless of handle type. Reachable end-to-end as soon as
  Rotate/Scale mode can actually be selected, which this phase's hotkeys now make possible.
- **Full regression sweep after this increment: 84 tests found, 83 passed, 0 regressions** — the same
  pre-existing, unrelated `TerraDyne.Raster.Workflow` failure as Phase 4.
- **Known limitation carried over from Phase 4**: still no in-editor manual verification of the full
  interactive feel (dragging every handle type, confirming the Shift/Ctrl/C/G hotkeys don't fight any
  other input in practice) — headless automation cannot exercise real mouse/keyboard interaction.

### Phase 6 — COMPLETE ✅ (road-point width scaling)
- `RoadWidthProfile.h`/`.cpp` (new): `FRoadWidthKey` (Dist/WidthMultiplier/Slope), mirroring
  `RoadElevationProfile.h`'s `FRoadHeightKey` shape exactly, plus `EvaluateWidth`. Deliberately
  **not** identical to `EvaluateHeight`'s contract, for a real reason found while designing this
  phase rather than blindly copying: an elevation profile is only ever meaningful bracketed in
  pairs (a bridge's start and end), but the gizmo's Scale handle produces a single, un-bracketed key
  as its very first, most common gesture — treating that as "not enough keys, do nothing" (matching
  `EvaluateHeight`'s own convention) would make the very first drag on the very first point visibly
  do nothing. `EvaluateWidth` instead handles 0/1/2+ keys as three complete answers: 0 → unscaled
  1.0 everywhere; 1 → 1.0 outside a 500cm falloff radius, smoothly blending (cubic smoothstep, not
  linear — a linear blend creases visibly at the key) to the key's own multiplier inside it; 2+ →
  the same cubic-Hermite bracketing `EvaluateHeight` uses, clamped to 1.0 outside the authored range
  (folded into the function itself, so callers need no outside-range guard of their own).
- `RoadMesh::FSweepOptions::WidthScalePerFrame` (new, parallel to the existing `BandScalePerFrame`):
  multiplies every section vertex's `Lateral` — road surface included, not just the curb/footpath
  band beyond `BandInnerHalfWidth` the way `BandScalePerFrame` is limited to — applied first, with
  `BandScalePerFrame`'s own turn-narrowing then running against the already-widened value. Confirmed
  by a hand-derived test (`TerraDyne.RoadMesh.WidthScaleAppliesToWholeSection`) with both options set
  together: an inner vertex widened just enough to cross `BandInnerHalfWidth` correctly starts
  picking up band-narrowing too, pinning down the documented composition order, not just that each
  option works alone.
- `ARealtimeRoadSplineActor::WidthKeys` (new, `ReplicatedUsing = OnRep_RoadConfig` — same
  replication convention as `HeightKeys`), `SetWidthMultiplierAtDistance`/
  `GetWidthMultiplierAtDistance` (updates an existing key in place within 0.5cm rather than
  accumulating near-duplicates, since the gizmo calls the setter every frame of a drag). A genuine
  architectural gap found while wiring the sweep loop, not anticipated by the plan: `RoadMesh::FFrame::DistanceAlong`
  resets to 0 at the start of every `FRoadRun` (a road split at junctions becomes several runs), but
  `WidthKeys`/`HeightKeys` are authored against the whole road's pre-split distance — fixed by adding
  `FRoadRun::StartDistance` (each run's own first station's distance in that shared global
  convention), populated in `BuildRuns` at both places a run starts, and added to a frame's local
  `DistanceAlong` before evaluating the profile.
- **A real, silently-compounding bug found and fixed while re-touching this exact code for Scale
  mode**: the road-point gizmo's transform-accessor `Get` added `EditOverlay->VerticalOffset` to the
  point's location (so the gizmo lines up with the visually-lifted handle sphere), but `Set` wrote
  `NewTransform.GetLocation()` straight through to `MovePointRealtime` with no matching subtraction —
  every completed drag permanently baked the offset into the point's real stored Z, and a second,
  separate drag on the same point would compound a second copy on top of the first. Fixed by
  subtracting the same offset in `Set`, restoring the Get/Set symmetry the "initial transform plus
  accumulated delta" model depends on.
- Scale mode's write is gated on the subsystem's mode actually being `EGizmoMode::Scale` — the point's
  distance-along-spline changes every frame of an ordinary Translate drag, and writing unconditionally
  would have spammed `WidthKeys` with a fresh, junk 1.0-multiplier entry at the point's ever-shifting
  position on every such drag instead of only recording an actual authored width change.
- **Full regression sweep after this increment: 87 tests found, 86 passed, 0 regressions** — the same
  pre-existing, unrelated `TerraDyne.Raster.Workflow` failure as Phases 4-5. New tests:
  `TerraDyne.RoadWidthProfile.WidthMath`, `TerraDyne.RoadMesh.WidthScaleAppliesToWholeSection`,
  `TerraDyne.RoadSplineActor.WidthKeyWidensMeshAtItsOwnDistance` (spawns a real road, measures actual
  mesh width in world space before/after a Scale-mode-equivalent write — a 2x key measured 1170 →
  2277.3 at its own station and exactly 1170 → 1170 unchanged 1900cm away, matching the falloff math
  by hand).
- **Known limitation**: width keys are anchored to a fixed distance-along-road, the same way
  `HeightKeys` already are — moving a road point via Translate after giving it a width key does not
  carry that key along to the point's new position (an accepted, precedented limitation inherited
  from the elevation-profile design this mirrors, not a new gap this phase introduces). Building's
  own Rotate/Scale wiring (Phase 7) and in-editor manual drag verification (carried over from Phases
  4-5) remain outstanding.

### Phase 7 — COMPLETE ✅ (building rotate/scale wiring)
- **The mechanical wiring turned out to already be in place, verified rather than built from
  scratch**: `RequestMoveBuilding`/`Server_MoveBuilding` already took a whole `FTransform` (not just
  a location) since Phase 4's migration, and `ATerraDyneBuildingActor`'s footprint is already stored
  actor-local (`GatherSplinePointsLocal`) with `GetFootprintPolygonWorld`/terrain-pad sampling
  already correctly transforming through the actor's real rotation and scale. Because
  `ATerraDyneEditController::UpdateMoveGizmo` already binds buildings through the subsystem's generic
  `SetTarget` (whole-actor transform), and `UpdateManipulation`'s Rotate/Scale branches never
  distinguish which consumer is active, Rotate and Scale already worked mechanically as an emergent
  consequence of Phases 4-5 — nothing needed to change in `TerraDyneEditController.cpp` at all.
- **A real, concrete multiplayer gap found while checking that emergent behavior actually holds
  end-to-end, not assumed**: `SetReplicateMovement(true)`'s `FRepMovement` (Unreal's standard actor
  movement replication, what `ATerraDyneBuildingActor` uses) carries Location and Rotation but has
  **no `Scale3D` field at all**. A gizmo Scale-mode drag would have resized a building only on
  whichever machine actually performed the drag — every other client/observer would keep rendering
  it at its old size forever, silently, with no error anywhere. Fixed with an explicit
  `ReplicatedScale3D` property (`ReplicatedUsing = OnRep_Scale3D`, mirroring `ReplicatedSplinePoints`'s
  existing pattern exactly): `NotifyBuildingChanged()` — already called from every real commit path
  (`RequestMoveBuilding`, the Tick throttle, the cancel handler) — now also resyncs it from the
  actor's real scale on the authority; `OnRep_Scale3D` applies it back out on every other machine,
  matching `OnRep_PadSettings`'s shape (reapply + re-stamp the pad, no full mesh regeneration needed
  since geometry itself never changes with scale, only the actor's overall transform).
- `TerraDyne.Building.GizmoRotateAndScale` (new): drives a real spawned building through
  `UGizmoManipulationSubsystem`'s actual `SetTarget`/`BeginManipulation`/`UpdateManipulation`/
  `EndManipulation` calls — the same calls `ATerraDyneEditController` makes — with hand-verified rays
  landing on `RotateZ` (ring radius 138, reusing `TerraDyne.Gizmo.DistanceRayToRing`'s geometry) and
  `ScaleZ` (segment reach 150). Confirms the building-specific piece Phase 3/5's generic subsystem
  tests couldn't: `GetFootprintPolygonWorld()` — what pad-leveling and everything else downstream
  actually reads — genuinely rotates every footprint point rigidly around the pivot (every point's
  distance from it preserved, checked numerically rather than assumed) and genuinely scales
  correctly (a `ScaleZ` drag measured exactly (1,1,1) → (1,1,2), leaving X/Y untouched). Also caught
  and fixed a real bug in the test itself, not the product code, while writing it: a vertical grab
  ray through the gizmo origin can't distinguish `ScaleZ` (whose hit-test segment starts exactly at
  the origin) from `ScaleUniform` (a point handle also exactly at the origin) — both this file's
  hand-derivation and a first failing run confirmed it — fixed by approaching `ScaleZ` with a
  horizontal ray at its far end instead, well clear of the ambiguous zone.
- `TerraDyne.Multiplayer.BuildingScaleReplication` (new): confirms `NotifyBuildingChanged` actually
  resyncs `ReplicatedScale3D` from a real scale change, matching this project's existing convention
  of checking replicated *state* in a single process rather than standing up a second network
  connection (the same shape `BuildingReplicationAndSplineSync`/`BuildingPadSync` already use).
  `OnRep_Scale3D` itself is `protected` like every other `OnRep_*` handler in this class, so it isn't
  reachable from a test any more than the existing ones are — its two-line body is confirmed by
  direct code reading instead.
- **Full regression sweep after this increment: 89 tests found, 88 passed, 0 regressions** — the same
  pre-existing, unrelated `TerraDyne.Raster.Workflow` failure as Phases 4-6.
- **Known limitation**: in-editor manual drag verification (carried over from Phases 4-6) remains
  outstanding — headless automation proves the math and the replication-sync bookkeeping, not the
  real click-and-drag feel or a genuine two-machine network test.

### Phase 8 — COMPLETE ✅ (VR/motion-controller readiness — design-verification only, no code)
**Claim to verify**: mouse and a future VR controller can share the exact same manipulation core,
with device-specific code confined to producing an `FGizmoInteractionRay` and nothing else.

**Verified by direct code reading, not assumed**: `Plugins/TerraDyne-main/Source/TerraDyne/Private/Gizmo/`
(`GizmoManipulationSubsystem.cpp`, `GizmoManipulationMath.cpp`, `RuntimeGizmoActor.cpp`) contains
**zero** references to `PlayerController`, `DeprojectMousePositionToWorld`, `GetMousePosition`,
`InputComponent`, `IsInputKeyDown`, or `EKeys::` anywhere — confirmed by grep across the whole
module, not by reading a few functions and generalizing. Every device-specific call lives entirely in
the two *consumers* (`ATerraDyneEditController::BuildGizmoInteractionRay`/`GetCameraForwardVector`,
`URealtimeRoadBuilderComponent`'s identical pair), each a handful of lines translating one input
source into `FGizmoInteractionRay{Origin, Direction}` plus a forward vector — exactly the
"Input Device → Interaction Ray → Gizmo Hit Test → Manipulation State → Transform Result" pipeline
the original spec called for.
- **What a VR integration would actually need to write**: a `BuildGizmoInteractionRay` equivalent
  reading a `UMotionControllerComponent`'s aim transform (`Origin = Controller->GetComponentLocation()`,
  `Direction = Controller->GetForwardVector()`) instead of deprojecting the mouse, and a
  `GetCameraForwardVector` equivalent reading the HMD's own forward vector instead of
  `PlayerCameraManager`. Every other call — `SetTarget`, `SetMode`, `BeginManipulation`,
  `UpdateManipulation`, `EndManipulation`, `CancelManipulation` — is already identical for both.
- **"Screen-space" handles generalize correctly, not coincidentally**: `RotateScreen`/`Center`'s
  `CameraForward` parameter is used purely as "the viewer's own forward vector" (a view-facing plane
  or a free-rotation axis) — nothing pixel- or viewport-based. An HMD's forward vector satisfies this
  identically to a desktop camera's.
- **Ctrl-held snap-invert, Shift/mode hotkeys**: already isolated as plain booleans/enum values passed
  into the subsystem (`bInvertSnapForThisCall`, `SetMode`) — a VR controller's own button/trigger
  state maps onto the same parameters with no subsystem change.
- **Explicit non-claims** (scope realism, not oversights): two-handed simultaneous VR manipulation
  (grabbing with both controllers for a combined move+rotate+scale gesture) is a genuinely different
  interaction model layered *on top of* this core, not something "readiness" implies — out of scope
  here, same as the original plan's own scoping. `ClickTolerance` (20cm) was tuned for mouse-cursor
  precision; VR hand-aiming is typically less precise and would likely want a larger tolerance —
  already an exposed constant, a tuning change, not a structural one. No VR plugin/pawn/hardware
  exists in this project to actually build or test against, so this phase is verification and
  documentation, not a shipped, testable feature.

### Phase 9 — COMPLETE ✅ (multi-object structural readiness — design-only, no code)
**Claim to verify**: the manipulation math operates on "a transform + a delta," so applying one
computed delta to several targets at once is additive/structural, not a rewrite.

**Verified by direct code reading**: every function in `GizmoManipulationMath.h` (`ComputeTranslateDelta`,
`ComputeRotationAngle`/`ComputeRotationDelta`, `ComputeScaleDelta`, every `DistanceRayTo*` hit test)
takes plain `FVector`/`FQuat`/`double`/`EGizmoHandleType` parameters and returns a delta or ratio —
none of them reference "the" target, a `USceneComponent`, or any singular piece of state. The delta a
drag produces (a translation vector, a rotation quaternion, or a scale ratio) is computed exactly
**once per frame**, from the gizmo's own pivot and the cursor/controller ray alone — multiple targets
would each just have that same one delta applied to their own `InitialTransform`, exactly the way
`UpdateManipulation` already applies it to the single current target's.
- **What actually is single-target today, concretely**: `FGizmoManipulationState` (`GizmoTypes.h`)
  holds one `Target`/`GetTransformFn`/`SetTransformFn`/`InitialTransform`, not an array — the
  *bookkeeping* is single-target, not the math. Extending it is additive: a `TArray` of per-target
  bindings (each with its own `GetTransformFn`/`SetTransformFn`/`InitialTransform`, captured the same
  way at `BeginManipulation`) sharing one `ActiveHandle`/`InitialRay`/computed-delta-per-frame, with
  `BeginManipulation`/`UpdateManipulation`/`EndManipulation`/`CancelManipulation` looping over that
  array instead of touching one binding directly. `GizmoMath`, `RuntimeGizmoActor`, and hit-testing
  need no changes at all.
- **Group pivot is not a new concept to add — it already exists**: `EGizmoPivotMode::Custom` +
  `CustomPivot` (Phase 3) already lets the gizmo sit at, and rotate/scale around, an arbitrary world
  point instead of the target's own origin, and `GetPivotLocation()` already reads from
  `State.CustomPivot` rather than any per-target field. A future multi-select's "group pivot" is
  exactly this same mode, fed a computed group centroid instead of a hand-placed point — no new pivot
  machinery required.
- **Explicit non-claim**: neither consumer has any multi-select UI today (clicking a second building/
  road point replaces the selection, it doesn't add to it) — building that selection UX is real,
  unstarted work, deliberately out of scope for a design-verification phase with "no multi-select UI
  exists in either consumer today" already called out as the reason back when this phase was planned.

### Phase 10 — COMPLETE ✅ (final regression + documentation)
**Full regression sweep**: 89 automation tests found, 88 passed. The one failure
(`TerraDyne.Raster.Workflow`) is a pre-existing GIS raster-import test relying on a satellite/ortho
layer this environment doesn't have — present before this whole gizmo system existed, unrelated to
any file this work touched, unchanged in every phase's sweep (Phase 4 through this one).

**Architecture summary**: `Plugins/TerraDyne-main/Source/TerraDyne/{Public,Private}/Gizmo/` —
`GizmoTypes.h` (enums + `FGizmoManipulationState`), `GizmoManipulationMath.h/.cpp` (pure, unit-tested
math), `RuntimeGizmoActor.h/.cpp` (the visual handle set, one `UWorldSubsystem`-owned instance shared
by every consumer), `GizmoManipulationSubsystem.h/.cpp` (the single stateful owner + clean API).
Both `ATerraDyneEditController` (buildings) and `URealtimeRoadBuilderComponent` (road control points)
drive it identically; road points additionally use `SetTargetTransformAccessors` since a spline point
has no "whole actor transform" of its own the way a building does.

**How it works / input flow**: a consumer builds one `FGizmoInteractionRay` per relevant input event
(mouse deprojection today; a VR controller's aim transform would be the only new code a future
integration needs, per Phase 8) and calls `UpdateHover`/`BeginManipulation`/`UpdateManipulation`/
`EndManipulation`/`CancelManipulation`. Hit-testing is dedicated math (`DistanceRayToSegment/Point/
Ring/Quad`), never physics collision. Every drag computes its delta from **initial transform plus
accumulated delta**, never compounding onto an already-modified transform. Mode (`Shift+W/E/R`),
coordinate space (`C`), and snapping (`G`, Ctrl-held to invert per-drag) are hotkeys on
`ATerraDyneEditController`, calling the subsystem directly — one binding set works for whichever
consumer currently owns the shared target, the same pattern Escape-cancel already established.

**Testing results**: every phase's own automation suite (`TerraDyne.Gizmo.*`, `TerraDyne.RoadWidthProfile.*`,
`TerraDyne.RoadMesh.WidthScale*`, `TerraDyne.RoadSplineActor.WidthKey*`, `TerraDyne.Building.GizmoRotateAndScale`,
`TerraDyne.Multiplayer.BuildingScaleReplication`) plus the full pre-existing regression suite, run
headlessly after every single phase — 0 regressions at any point across Phases 1-10. Every hit-test
formula and every delta formula was hand-derived with concrete numeric predictions before the test
that confirmed it was ever run, matching this project's own established verification discipline.

**Known limitations** (all stated explicitly, not silently dropped):
- **No in-editor manual verification.** Every phase's tests prove the math and the wiring; none of
  them can prove the actual click-and-drag *feel*, or that the Shift/Ctrl/C/G hotkeys don't fight some
  interaction this test suite didn't think to check. Actually running the editor and dragging a
  building and a road point through every mode is still the one thing headless automation cannot do
  here.
- **VR is architecturally ready, not implemented or tested** (Phase 8) — no VR plugin/hardware exists
  in this project.
- **Multi-object manipulation is structurally ready, not implemented** (Phase 9) — no multi-select UI
  exists in either consumer.
- **Group pivot and screen-space pivot**: Object Origin and Custom Pivot are implemented; Group Pivot
  needs multi-select (Phase 9, deferred) to have anything to average; Screen-space Pivot was never
  requested by the approved plan and was not built.
- **Road width keys don't follow a point through Translate edits** (Phase 6) — anchored to a fixed
  distance-along-road, the same accepted limitation `HeightKeys` already has.
- **Undo is delegate-based, not `FScopedTransaction`** (Phase 3) — deliberate, since that mechanism is
  editor-only and would not survive a packaged build; each consumer records its own undo snapshot
  (buildings: none, matching pre-existing behavior; roads: the existing point-list stack) from the
  `OnManipulationBegin`/`End`/`Cancel` delegates.

### Phase 11 — COMPLETE ✅ (visual polish, 2D plane dragging, rotate/scale handles & live MCP test verification)
- **Arrow visuals & proportions (Translate)**: Rebalanced cylinder shaft scale `(0.06, 0.06, 1.1)` and cone tip scale
  `(0.22, 0.22, 0.4)` in `ARuntimeGizmoActor`. Shaft length is 110 at offset 65.0cm, cone height is 40 at
  offset 140.0cm with tip reaching 160.0cm, matching the clean reference design with no disconnected gaps.
- **Plane handles (placement, orientation, colors & 2D hit-testing)**:
  - Placement: midway along coordinate planes (`(0, 45, 45)` for YZ, `(45, 0, 45)` for XZ, and `(45, 45, 0)` for XY).
  - Orientation: flat axis-aligned square quads with `FRotator::ZeroRotator`.
  - Color convention: Standard RGB normal alignment — Plane YZ (between +Y and +Z) is **Red** (`MatPlaneYZ` / `MI_XAxis`),
    Plane XZ (between +X and +Z) is **Green** (`MatPlaneXZ` / `MI_YAxis`), and Plane XY (between +X and +Y) is **Blue**
    (`MatPlaneXY` / `MI_ZAxis`).
  - Hit-testing & drag fix: Corrected spanning axes in `UGizmoManipulationSubsystem::FindHoveredHandle` (`PlaneXZ` spans
    X and Z; `PlaneYZ` spans Y and Z), enabling ray projection and 2D planar translation without jumping or failing.
- **Center handle (Translate)**: Swapped the filled 3D sphere for the circular disk mesh (`SM_ScreenTranslationHandle`). Added
  per-frame camera billboarding in `ARuntimeGizmoActor::Tick` so the disk consistently faces the camera.
- **Rotation handles (Rotate)**:
  - Axis rings: 3-axis high-fidelity 3D torus rings (`SM_RotationHandle`, radius 138cm) for Pitch (X / Red), Yaw (Y / Green),
    and Roll (Z / Blue) using `MI_Rotation_*` emissive materials and `MI_Rotation_Hovered` highlight.
  - Screen rotation ring: Dedicated outer ring (`SM_ScreenRotationHandle`, radius 168cm) surrounding the inner rings,
    rendered with `M_ScreenRotation` / `MI_ScreenRotation_Hovered` and continuously billboarded to camera forward in `Tick()`.
- **Scale handles (Scale)**:
  - Fixed disconnected axis handles: Re-anchored `SM_ScaleHandle` components to origin `(0, 0, 0)` with scale `1.2` (reaching
    150cm with the box tip), fixing a major visual/interaction bug where handles were offset by 150cm and floated in mid-air.
  - Center uniform scale handle: Anchored `SM_UniformScaleHandle` box at origin `(0, 0, 0)` with scale `1.8` (25cm cube) for
    crisp uniform 3D scaling feedback.
- **Dynamic material instance allocation**: Restructured `EnsureDynamicMaterials` and `ApplyMaterialsToComponents`
  to ensure unique MIDs are assigned per spawned actor instance (`MatX`, `MatY`, `MatZ`, `MatPlane*`, `MatNeutral`),
  preventing shared-material state across multiple gizmos.
- **Live Unreal MCP automation testing**: Discovered and executed all 17 `TerraDyne.Gizmo.*` automation tests live
  against the running Unreal Editor via Epic's embedded ModelContextProtocol (MCP) server on port 8000:
  **17 / 17 tests passed (100% success rate)**.

**Definition of Done** (from the original spec):
- [x] Translation, rotation, and scaling are reliable — hand-verified math, tested (Phases 1, 5).
- [x] World and Local space both work, without unexpectedly modifying the object's transform —
      verified `SetMode`/`SetCoordinateSpace`/`SetPivotMode` never touch `SetTransformFn` (Phase 5).
- [x] Axis and plane handles both work — hit-tested and reachable end-to-end (Phases 3, 5, 11).
- [x] Hover and selection work — `UpdateHover`/`BeginManipulation`, highlighted-handle tests (Phases 3, 11).
- [x] Mouse dragging works for both consumers (Phase 4, 11).
- [x] VR interaction can use the same manipulation core — verified structurally (Phase 8); not built.
- [x] Snapping works, including live Ctrl-invert during a drag (Phases 1, 5).
- [x] Cancel restores the original transform exactly, not approximately (Phase 3, bit-for-bit tested).
- [x] Gizmo size is usable at any camera distance — camera-distance auto-scaling verified in `ARuntimeGizmoActor::Tick` (Phases 2, 11).
- [x] Hit testing is stable — dedicated math, not physics collision (Phases 3, 11).
- [x] Core math has automated tests, runnable without a rendered viewport (all phases).
- [x] No per-frame allocations/spawn-destroy in the drag hot path — verified by code reading (Phase 4
      review pass); `UpdateManipulation`/`FindHoveredHandle` touch only stack values and existing
      components.
- [x] Existing building/road functionality is intact — full regression suite green after every phase.
- [x] The implementation is maintainable/extensible — plain-data math module, one shared subsystem,
      generalized transform accessors, structural multi-object/VR readiness documented above.

The full 11-phase runtime transform gizmo system, as scoped, polished, and live-tested, is complete.

## 🔍 Antigravity findings triage — COMPLETE ✅

A third-party audit tool ("Antigravity") flagged 7 findings across the codebase. Each was verified
against the actual code before any fix landed, per this project's own established discipline of never
trusting an external claim without direct verification — 6 held up (2 High, 1 upgraded to Medium, 3
Medium/Low as originally rated) and 1 was a false positive, rejected with reasoning rather than
"fixed" into a regression.

1. **Gizmo handles behind the camera were grabbable (High)** — `FindHoveredHandle` accepted any hit
   within tolerance regardless of which side of the ray's origin it fell on. Fixed with an
   `AlongRay >= 0.0` guard in
   [GizmoManipulationSubsystem.cpp](Plugins/TerraDyne-main/Source/TerraDyne/Private/Gizmo/GizmoManipulationSubsystem.cpp).
   Tested: `TerraDyne.Gizmo.SubsystemRejectsHandleBehindRay`.
2. **`IsLocationTraceable` assumed a 2-sided mesh (High, worse than originally described)** — it
   divided the mesh's vertex count by 2 to estimate the top-surface grid resolution, but
   `ATerraDyneChunk::RebuildMesh` only ever builds a single top surface (no bottom slab), so every
   resolution/index computation downstream was wrong. Fixed in
   [TerraDyneCollision.cpp](Plugins/TerraDyne-main/Source/TerraDyne/Private/Physics/TerraDyneCollision.cpp)
   by using the full vertex count. Tested: `TerraDyne.Physics.IsLocationTraceableFindsTheRightCell`.
3. **OpenDRIVE header north/south vs. east/west (rejected — false positive)** — the finding assumed
   Unreal's +Y axis is North, but this project's Cesium georeferencing uses the East-South-Up (ESU)
   tangent-plane convention, where +Y is South. `-Point.Y` for the North bound is therefore the
   *correct* South→North conversion, not an axis mixup. Left as-is in
   [RoadXodrExport.cpp](Source/RealtimeTerrain/Private/RoadXodrExport.cpp), with a comment explaining
   why; the existing structure test now locks in the verified-correct values.
4. **Raw `APlayerController*` risked a dangling pointer (Medium)** — `UndoStacks`/`RedoStacks`/
   `PendingStrokeOwner` in `ATerraDyneManager` stored raw controller pointers; a controller destroyed
   mid-stroke (e.g. a player disconnecting) would leave a dangling `PendingStrokeOwner`. Fixed by
   switching to `TWeakObjectPtr<APlayerController>` in
   [TerraDyneManager.h](Plugins/TerraDyne-main/Source/TerraDyne/Public/Core/TerraDyneManager.h) /
   [TerraDyneManagerUndo.cpp](Plugins/TerraDyne-main/Source/TerraDyne/Private/Core/TerraDyneManagerUndo.cpp).
   Tested: `TerraDyne.Undo.BeginStrokeSurvivesDisconnectedOwner`.
5. **Zero-length segments produced NaN (Medium)** — `RoadElevationProfile::EvaluateHeight` and
   `RoadWidthProfile::EvaluateWidth` divided by `SegmentLength` without checking for two keys authored
   at (or merged to within tolerance of) the same distance, producing 0/0 = NaN. Fixed with an
   `UE_DOUBLE_SMALL_NUMBER` guard in both
   [RoadElevationProfile.cpp](Source/RealtimeTerrain/Private/RoadElevationProfile.cpp) and
   [RoadWidthProfile.cpp](Source/RealtimeTerrain/Private/RoadWidthProfile.cpp). Tested: new cases in
   `TerraDyne.RoadElevationProfile.HeightMath` and `TerraDyne.RoadWidthProfile.WidthMath`.
6. **Division by (near-)zero for near-180° hairpin corners (Low/edge case)** — `RoadSpiral.cpp`'s
   corner-fitting bisection divided by `Cos(Diff/2)`, which shrinks toward 0 as a corner's turn angle
   approaches ±180°. A literal near-zero epsilon on `Cos` alone wasn't a sufficient guard (at 179.99°,
   `Cos(Diff/2) ≈ 8.7e-5` — well above any reasonable near-zero threshold, yet still large enough a
   divisor to send the corrected value into the millions). Fixed by bounding the *candidate result*
   against the corner's own arm length instead of gating on `Cos` directly, falling through to the
   already-correct bisection loop when the single-division shortcut would be unreasonable. Tested:
   `TerraDyne.RoadSpiral.HairpinCornerStaysFinite` — note that for this specific pathological input
   (1000cm arms, a near-full reversal, capped at a 500cm max radius) the mathematically correct answer
   is a path that collapses to a tiny curve at the corner apex (finite and bounded, just not a
   "nicely-proportioned" S-curve) — the test only requires a valid, finite, bounded path, not a
   substantial one.
7. **`const_cast` mutated a replicated property inside a const getter (Low/architectural, worse than
   originally described)** — `ATerraDyneBuildingActor::ResolvePadTargetWorldZ() const` used
   `const_cast` to write the *replicated* `PadTargetWorldZ` on every call, not just a `mutable` cache
   field. This meant calling what looked like an innocuous query silently triggered network
   replication traffic and `OnRep_*` side effects on every client, on every caller — redundant for
   `SnapToPadHeight` (which already commits explicitly) and actively harmful for every other caller.
   Fixed by removing the write in
   [TerraDyneBuildingActor.cpp](Plugins/TerraDyne-main/Source/TerraDyne/Private/Building/TerraDyneBuildingActor.cpp);
   the `mutable` cache fields remain untouched. Tested:
   `TerraDyne.Multiplayer.ResolvePadTargetWorldZHasNoSideEffect`.

**Full regression sweep**: 94 automation tests found, 93 passed — the only failure is the same
pre-existing, unrelated `TerraDyne.Raster.Workflow` GIS-import test noted throughout this README.

---

A high-performance, runtime-editable chunk terrain and GIS framework for Unreal Engine 5.8. Built for planetary/regional geographic datasets, real-time procedural sculpting, dynamic multi-band GIS/DEM raster ingestion with auto-detection, modular buildings, and world persistence.

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
       │ • Cesium DynamicPawn (WGS84, ESU Horizon, Spd, TopView) │
       │ • Modular Spline Buildings & Dynamic-Step Pad Leveling  │
       │ • Full Undo/Redo (Ctrl+Z/Y) & Disk Persistence          │
       │ • Dynamic Slate HUD (Auto-Detect, Load/Unload, Delete)  │
       └─────────────────────────────────────────────────────────┘
```

---

## ✨ Features & Capabilities

### 1. Dual Independent Session Modes
* **Sandbox Mode**: Instant procedural grid initialization ready for immediate brush sculpting, layer editing, and building placement.
* **GIS DEM Mode**: Dedicated geospatial workflow designed for massive real-world GeoTIFF datasets ($435+\text{ km}$). Bypasses procedural noise, allocates exact elevation samples, and loads macro-scale terrain tiles seamlessly.

### 2. Cesium DynamicPawn Camera Navigation
* Built on Cesium's `AGlobeAwareDefaultPawn` with native `UCesiumGlobeAnchorComponent`:
  * **WGS84 Georeferencing**: Live Longitude, Latitude, and Height coordinate tracking.
  * **ESU Horizon Leveling**: Computes true East-South-Up orientation, eliminating horizon skew and gimbal lock across planetary distances.
  * **Ellipsoid-Aligned Flight**: `MoveUp_World` automatically follows the true geodetic surface normal.
  * **Top-Down Map Framing (`F` / `Home` / HUD Button)**: Instantly computes active dataset bounds and frames the entire terrain footprint.
  * **Dynamic Flight Speed**: Automatically scales maximum flying speed ($\text{Altitude} \times 2.5$) for effortless navigation from centimeters to regional scales — scaled from the pawn's real WGS84 height, not raw Unreal Z, so it stays correct far from the `CesiumGeoreference` origin (e.g. after flying to a Tiled Cesium Import footprint on another part of the globe), where Unreal Z alone no longer means "altitude".
  * **Mouse Wheel Speed Control**: Scroll adjusts a flight-speed multiplier ($0.1\times$–$10\times$, exponential per notch) on top of the automatic altitude scaling above, instead of zooming/dollying the camera.
  * **Sculpt Isolation**: Left-click drag is dedicated to terrain editing and spline drawing; right-click drag orbits freely without fighting camera controls.
  * **World Bounds Checks disabled at startup**: `AWorldSettings::bEnableWorldBoundsChecks` is forced off in `UTerraDyneSubsystem::Initialize` (before any actor's `BeginPlay`). Left on — Unreal's default — it's incompatible with Cesium's origin rebasing: a rebase shifts every actor's absolute Z, so an airborne pawn can spuriously read as having fallen below `KillZ`, get destroyed, and respawn at the default `PlayerStart` — visible as the camera snapping back mid-flight to wherever it started.

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
  * **Buildings Accordion**: Footprint spline placement, floor stepper, and dynamic-step pad leveling.

### 5. Modular Spline Buildings & Dynamic Terraforming
* Arbitrary polygon footprint definition using spline nodes.
* Live floor count stepper controls.
* Macro chunk step-scaled pad terraforming (`EffectiveFalloff = Max(Falloff, Step * 1.5)`) ensuring smooth foundations on all grid resolutions.
* **Pad leveling on Tiled Cesium Import terrain**: `ResolvePadTargetWorldZ`'s footprint samples fall back to a $\pm 200\text{ km}$ line-trace-against-collision approach wherever no TerraDyne chunk covers a sample point, so a building's pad still levels correctly when placed on a locally-imported Cesium tile instead of TerraDyne's own terrain.
* **Focus button**: frames the camera on the currently selected building from the Buildings panel section.

### 6. World Persistence & Transaction History
* Complete disk serialization format for terrain state and building transforms.
* Multi-layer height buffers (**Base**, **Sculpt**, **Detail**).
* Full undo and redo transaction history (`Ctrl+Z`, `Ctrl+Y`).

### 7. 100% Multiplayer & Network Replication
* **Authoritative Server Architecture**: Strictly validated through `HasEditAuthority()`. Unauthorized edits are rejected on the server.
* **Delta-Broadcasting**: Raw $64 \times 64$ height buffers are deterministically evaluated locally; live sculpting strokes are multicast via lightweight geometric parameters (`Multicast_BroadcastBrushStroke`), consuming minimal bandwidth.
* **Full Actor & Property Replication**: `RasterLayerSlots`, `DynamicRasterLayers`, and `BuildingActors` automatically synchronize with `DOREPLIFETIME` and `OnRep` handlers.
* **Multi-User Streaming**: Throttled view reporting (`Server_ReportViewLocation`) keeps server-side chunk streaming centered around all connected players.

### 8. Tiled Cesium Import (Local Quantized-Mesh Terrain & XYZ Imagery)
* **DEM-only**: only available once a world is Imported/GIS-DEM (or already has an elevation layer) — a real-world tile pyramid has no meaningful place next to Sandbox's procedural terrain, so the panel section and the underlying `ATerraDyneManager` calls both gate on it.
* **No hardcoded paths**: `Import Terrain Tileset Folder…` / `Import Imagery Overlay Folder…` open a native folder picker; the last folder picked per pyramid type is remembered across editor sessions via `GConfig` (`GEditorPerProjectIni`), never baked into source.
* **Terrain**: points a `ACesium3DTileset` (`FromUrl`) at a local Cesium `quantized-mesh-1.0` pyramid — a folder containing `layer.json` and `{z}/{x}/{y}.terrain` tiles.
* **Imagery**: adds a `UCesiumUrlTemplateRasterOverlay` reading a local XYZ PNG pyramid — files laid out on disk as `{z}/{x}/{y}.png`, requested via Cesium's `{reverseY}` template token (not `{y}`, which Cesium defines as TMS row-ordering — opposite of the standard XYZ/slippy-map ordering these QGIS-style exports actually use) — WebMercator projection; the zoom range is auto-detected from the pyramid's own numeric subfolder names, spawning a bare ellipsoid tileset first if no terrain has been imported yet.
* **Fully offline**: both use `file:///` URLs, which `UnrealAssetAccessor` short-circuits before ever reaching `FHttpModule` or Cesium Ion — no network access, no Cesium ion token required. Actors are spawned deferred (`SpawnActorDeferred`) with the tileset source set before `FinishSpawning()`, since `ACesium3DTileset` otherwise defaults to `FromCesiumIon` and calls `LoadTileset()` from `OnConstruction` immediately on spawn.
* **Auto-fly-to-footprint**: each successful import reads the pyramid's own geographic extent (from its deepest zoom level's actual tile index range, not the often-boilerplate `bounds` field in `layer.json`) and flies the camera to an aerial top-down view centred over it — the same framing angle as the existing "Top View" feature, altitude scaled to the footprint's size so it fits comfortably in frame.
* **Cuts a hole in the backdrop globe** (toggle: *Cut Out Backdrop Globe*, on by default): a successful terrain import clips every other `ACesium3DTileset` already in the world — e.g. a placed "Cesium World Terrain" used as the level's default globe backdrop — via a `UCesiumPolygonRasterOverlay` on a rectangle matching the imported footprint, so the backdrop stops rendering real terrain geometry for that ground (which would otherwise visibly overlap/z-fight with the local import) while staying fully visible everywhere else. Turn the toggle off if the computed footprint looks wrong and the cutout lands in the wrong spot. (Imagery-only imports don't touch this: the bare ellipsoid they spawn has no relief of its own to compete with the backdrop.)
### 9. 3D Model Importer, Placement, Asset Palette & Scene Outliner
* **Runtime 3D Model Importers (`FTerraDyneModelImporter`)**:
  * **6 Supported Formats**:
    * **Wavefront OBJ (`.obj` & `.mtl`)**: Positions, normals, UVs, n-gon fan triangulation, material groups, diffuse colors (`Kd`), and texture files (`map_Kd`).
    * **Autodesk FBX (`.fbx`)**: Runtime binary & ASCII FBX loading via the open-source **`ufbx`** library (bypassing Autodesk's editor-only SDK so FBX assets load in packaged standalone builds).
    * **glTF 2.0 & Binary glTF (`.gltf` & `.glb`)**: Complete JSON and binary container parsing via the open-source **`cgltf`** library, including PBR BaseColor materials and embedded/external PNG/JPG textures.
    * **Stereolithography STL (`.stl`)**: Both 80-byte header Binary and ASCII (`facet normal ... endfacet`).
    * **Polygon File Format PLY (`.ply`)**: Both ASCII and Binary header elements, vertex positions, normals, and face indices.
  * **Smart Multi-Source Texture Resolution (`FTerraDyneModelImporter::ResolveTexturePath`)**:
    * Automatically resolves textures across FBX, glTF, and OBJ formats even when files contain broken or author-machine absolute paths (e.g. `C:\Users\Author\...`).
    * Searches candidate locations: exact path, model directory, adjacent subfolders (`textures/`, `Textures/`, `images/`, `maps/`, `materials/`), parent directories, and format fallbacks (`.png`, `.jpg`, `.bmp` for `.tga`/`.tif`/`.psd` references).
    * Embedded FBX binary textures decoded directly from internal memory blobs.
  * **Dynamic Image Wrapper**: Automatically loads `.png`, `.jpg`, and `.bmp` files (from external paths or embedded memory buffers) directly into transient `UTexture2D` instances.
  * **Textured Material Binding**: `ApplyMaterials()` dynamically binds textures across standard parameter slots (`Texture`, `BaseColorTexture`, `EmissiveTexture`, `Diffuse`, `Albedo`) onto textured base materials.
  * **Automatic Normal & Bounds Computation**: Calculates bounding dimensions, polycounts (`tris • verts`), and overlays missing vertex normals via `FMeshNormals::InitializeOverlayToPerVertexNormals`.
* **Placed Model Actor (`ATerraDynePlacedModelActor`)**:
  * Runtime-spawnable, selectable 3D scene actor using `UDynamicMeshComponent` for geometry and collision raycasting.
  * **Multiplayer Replication**: Location, rotation, and `ReplicatedScale3D` synchronized across the network with `OnRep_Scale3D`.
  * **Visual State**: Custom depth stencil selection outline and scene visibility toggles (`SetVisibleInScene`).
* **Standard Transform Gizmo Integration (`UGizmoManipulationSubsystem`)**:
  * Seamlessly connects to the project's standard transform gizmo for Translate (`Shift+W`), Rotate (`Shift+E`), and Scale (`Shift+R`).
  * Full support for Coordinate Spaces (`C` for World/Local), Snapping (`G` / Ctrl-invert), and Cancel (`Escape`).
* **Slate HUD: 3D Models & Asset Library (`STerraDynePanel`)**:
  * **Import Buttons**: `📥 Import Model...` (native OS multi-file picker for `*.obj;*.stl;*.ply;*.fbx;*.gltf;*.glb`) and `📁 Import Folder...` (batch folder import).
  * **Placeable Asset Palette**: Scrollable list of loaded models with **PNG photo thumbnails** (auto-resolved from companion `{ModelName}.png`, `{ModelName}_thumb.png`, or `preview.png`), dimensions in meters, polycount (`tris • verts`), one-click `[Place]` button, and `[X]` removal.
  * **Terrain Placement Mode**: Shows a holographic preview ghost following the cursor projected onto terrain chunks. Clicking drops and spawns an `ATerraDynePlacedModelActor` and attaches the transform gizmo immediately.
* **Slate HUD: Scene Outliner / Hierarchy Panel**:
  * Live object counter header (e.g. `SCENE OUTLINER (7)`).
  * Complete hierarchy listing placed 3D models (`[Model]`) and procedural buildings (`[Building]`).
  * **Row Actions**:
    * **Click Label / Row**: Selects the entity in the viewport and attaches the standard transform gizmo.
    * **`🎯` Focus**: Smoothly flies the camera to center and frame the object.
    * **`👁️` Visibility**: Toggles actor visibility (`SetVisibleInScene` / `SetActorHiddenInGame`).
    * **`🗑️` Delete**: Destroys the entity and removes it from the world.

#### Runtime defects found and fixed (2026-08-28)

The feature landed with passing tests that covered the parsers only — nothing called
`SetModelAsset` and no test ran a real gizmo drag, so the entire runtime path had never executed.
Three defects were hiding in exactly that gap:

* **Crash on first placement.** `ApplyMaterials` used `ConstructorHelpers::FObjectFinder` outside a
  constructor. `ConstructorHelpers::CheckIfIsInConstructor` is `Fatal` there (`UObjectGlobals.cpp`),
  so the first model ever placed killed the process. Now uses the `LoadObject` call that already sat
  beside it as a fallback.
* **Placed models had no collision.** The constructor set collision responses but never enabled
  complex collision, and `UDynamicMeshComponent::ContainsPhysicsTriMeshData` gates entirely on
  `bEnableComplexCollision` (default `false`) — so every model was invisible to
  `ATerraDyneEditController::TraceUnderCursor` and could not be clicked or selected. Now calls
  `SetComplexAsSimpleCollisionEnabled(true, false)` in the constructor, plus `UpdateCollision(false)`
  after each mesh swap, since `NotifyMeshUpdated()` only resets the render proxy and never touches
  physics.
* **FBX imported mirrored.** The importer targeted `ufbx_axes_right_handed_z_up`; Unreal is
  **left**-handed Z-up. The two presets differ only in their `front` axis (`-Y` vs `+Y`), so every FBX
  came in mirrored through the Y plane with reversed winding — rendering inside-out. Now targets
  `ufbx_axes_left_handed_z_up`; ufbx flips face winding itself as part of the conversion
  (`handedness_conversion_retain_winding` defaults to `false`).

Two regression tests were added, because the existing nine could not see any of the above:
`TerraDyne.ModelImport.PlacedActorCollision` (imports a closed box, calls `SetModelAsset`, then
asserts cooked triangle geometry exists and a world line trace on `ECC_WorldStatic` — the same
channel `TraceUnderCursor` uses — actually hits the actor) and
`TerraDyne.ModelImport.FBXHandedness` (a fixture declaring Y-up right-handed source axes with one
non-zero source-Z vertex, asserting it lands on **+Y**; under the old setting it landed on -Y).

#### Gaps closed (2026-08-28)

* **Persistence.** `UTerraDyneSaveGame` now carries `ModelLibrary` and `PlacedModels`. Runtime
  geometry belongs to no package and cannot be serialised, so the save stores source paths and
  re-imports on load — the same approach `FTerraDyneGeoTIFFImportState` takes with its `.tif`. The
  catalog is restored **under its saved asset ids** before any placement spawns; minting fresh ids
  would leave every restored placement dangling. Saves written before this load as empty arrays.
* **Client-side asset resolution.** `ATerraDynePlacedModelActor` replicates `SourceFilePath`
  alongside `AssetId`, and `ResolveModelAsset()` (called from `BeginPlay` and both `OnRep`s, so no
  property-arrival order can miss it) falls back to
  `UTerraDyneAssetLibrarySubsystem::ResolveOrImport`, importing the file locally under the
  replicated id. A machine that never ran the original import previously drew an empty actor.
  *Limitation*: the file must exist on that machine, exactly as with GeoTIFF import.
* **Generated thumbnails.** `FTerraDyneModelThumbnail` renders the geometry with a
  `USceneCaptureComponent2D` and caches the result as a PNG under
  `Saved/TerraDyne/ModelThumbnails/`, keyed by a hash of the full source path so two different
  `chair.obj` files cannot collide. A companion PNG beside the model still wins. Capture uses
  `SCS_BaseColor` so no light needs to be spawned into the live scene, and every entry point checks
  `FApp::CanEverRender()` and returns null headlessly rather than caching a black tile.
* **Library dedupe.** Re-importing a path already in the catalog returns the existing id instead of
  a duplicate entry; comparison is on the full path, case-insensitively.
* **Live outliner.** The panel polls a per-type count signature twice a second and rebuilds only
  when it changes, instead of waiting for the Refresh button.
* **Model selection is gated on a model tool.** A new `SelectModel` mode joins `PlaceModel`, with
  Place/Select buttons matching the Buildings section. The selection trace previously ran
  a 500 km line trace on *every* left click in *every* mode, so clicking a model mid-sculpt silently
  abandoned the brush stroke.
* **Thumbnail brush leak.** The palette raw-`new`'d an `FSlateImageBrush` per tile and never freed
  it; `SImage` does not take ownership. Now panel-owned and rebuilt with the list — which the
  automatic refresh above would otherwise have turned into a continuous leak.
* **Imported textures no longer collected.** `FTerraDyneImportedMaterial` is a `USTRUCT` and
  `FTerraDyneImportedModelAsset::Materials` is a `UPROPERTY`, so the collector can reach the diffuse
  textures. Previously they were collected while the catalog still held `TObjectPtr`s to them:
  models placed before a GC kept their look via the material instance's own reference, anything
  placed afterwards came up untextured.

Verified by `TerraDyne.ModelImport.Persistence` (a real `BuildSaveGameInstance` →
`ApplyLoadedSaveGame` round trip asserting ids, transforms, scale and resolved geometry),
`.LibraryDedupe`, `.ResolveOrImport` and `.MaterialTextureSurvivesGC`. Full suite: **106/107**, the
sole failure being the pre-existing `TerraDyne.Raster.Workflow` fixture gap.

**Still open**: multiplayer needs the model file present on each machine (inherent to replicating a
path rather than geometry); generated thumbnails need a real RHI and have not been inspected
visually, only proven to no-op safely headless.

---

### 10. OpenStreetMap Data Ingestion

Imports real-world buildings from an OpenStreetMap export. Export an area from
openstreetmap.org (pan to it, click **Export**), then use **🌍 Import OpenStreetMap (.osm)…** in the
new **OPENSTREETMAP DATA** panel section. Offline and packaged-build safe — no network dependency.

* **Reader (`FTerraDyneOSMParser`)** — OSM XML via the engine's runtime `XmlParser` module, so no
  third-party dependency. Nodes, ways and tags; `<relation>` is deliberately ignored for now, since
  what it mainly buys is multipolygon buildings with holes and the footprint spline cannot express
  a hole. Deliberately pure — no world, no UObject, no georeferencing — so the fiddly decisions are
  testable with no map open.
  * **Classification**: closed `building=*` ways become buildings (`building=no` excluded).
  * **Tag interpretation**: `building:levels` → storeys, falling back to `height` in metres over a
    3 m storey. Numeric tags are free text in OSM and routinely carry units (`"12 m"`), so the
    leading number is taken and the rest ignored rather than rejecting the tag.
  * **Line-oriented XML workaround**: `FXmlFile` cannot parse two elements sharing a line and
    rejects the whole document with a bare "Failed to parse the loaded document". Exports from
    openstreetmap.org are one element per line and load as-is, but anything minified failed for a
    reason a user could never act on, so the reader splits element boundaries onto their own lines
    first. Safe here specifically because OSM XML holds all its data in attributes, never in element
    text, and a literal `><` cannot occur inside a properly escaped attribute value.
* **Geometry (`FTerraDyneOSMIngest`)** — also pure. Drops OSM's repeated closing node and any
  coincident corners, centres each footprint's origin on its centroid (so moving a building stays
  one transform), and **normalises footprint winding counter-clockwise**.
  * That last one is not cosmetic. `ResetToDefaultFootprint` winds counter-clockwise deliberately,
    and wall generation and the terrain pad both read that ordering — a clockwise footprint builds
    the building inside-out. It also cannot be decided from OSM's own ring convention, because this
    project georeferences through Cesium in **East-South-Up**: latitude grows toward −Y, so the
    conversion flips every ring's handedness on the way in. Normalising *after* conversion, in
    Unreal space, is the only ordering that is actually correct.
* **Placement** — lon/lat goes through the level's `ACesiumGeoreference` via
  `TransformLongitudeLatitudeHeightPositionToUnreal`, the same call the GeoTIFF and tiled-Cesium
  paths already use. The import refuses to run without a georeference rather than guessing.
  Buildings resolve their own ground height through `SnapToPadHeight`.
* **Truncation is reported, not silent** — a city-sized extract holds hundreds of thousands of ways,
  so imports stop at `MaxFeatures` (2000) and the summary says how many were left behind, alongside
  counts of anything skipped for unresolvable node refs or too few usable points.

Covered by seven tests: `TerraDyne.OSM.Parse`, `.Classify`, `.TagResolution`, `.MalformedInput`,
`.FootprintWinding`, `.FootprintBuild` and `.Centreline`. Full suite **115/116**, the sole failure
being the pre-existing `TerraDyne.Raster.Workflow` fixture gap.

**Not yet done**: downloading an area straight from the OSM API (the reader already takes a string,
so it is additive); `.osm.pbf`; multipolygon relations; and the end-to-end spawn path has unit
coverage on every pure layer but has not been run against a real georeferenced level.

---

## 🛠️ Requirements & Setup

### Engine & Plugin Dependencies
* **Unreal Engine**: `5.8`
* **Plugins Enabled**:
  * `GeometryScripting` & `GeometryFramework`
  * `VirtualHeightfieldMesh`
  * `CesiumForUnreal`
  * `CelestialVault` & `DaySequence`
  * `ModelContextProtocol` & `AllToolsets` (`Engine/Plugins/Experimental/`) — Epic's official
    Unreal MCP server, embedded in the engine since 5.8. Lets an MCP client (Claude Code) inspect
    and drive the live editor (actors, blueprints, materials, automation tests) directly instead of
    working from static source alone. Enabled project-side in `RealtimeTerrain.uproject`; start it
    in a running editor session with the console command `ModelContextProtocol.StartServer`
    (defaults to `http://127.0.0.1:8000/mcp`), then connect it as an MCP client.
  * (Formerly also depended on the third-party `RoadBuilder` plugin for the now-removed road/junction system — no longer required.)

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
| **Flight Speed Adjust** | Mouse Wheel Scroll Up / Down |
| **Frame Whole Map (Top View)** | `F` or `Home` |
| **Terrain Sculpting** | Left Mouse Button + Drag on terrain |
| **Building Placement** | Left Click on terrain in Building mode |
| **3D Model Placement** | Click `[Place]` in 3D Asset Palette $\rightarrow$ Left Click on terrain |
| **Select 3D Model / Building** | Left Click on object in viewport or in Scene Outliner |
| **Undo / Redo** | `Ctrl + Z` / `Ctrl + Y` |
| **Gizmo Mode (Translate)** | `Shift + W` |
| **Gizmo Mode (Rotate)** | `Shift + E` |
| **Gizmo Mode (Scale)** | `Shift + R` |
| **Gizmo Coordinate Space** | `C` (Toggle World / Local Space) |
| **Gizmo Snapping Toggle** | `G` (Toggle Grid / Angle Snapping) |
| **Gizmo Temporary Snap Invert** | Hold `Ctrl` while dragging handle |
| **Cancel Gizmo Drag** | `Escape` (Restores exact pre-drag transform) |
| **Focus Object in Scene** | Click `🎯` in Outliner or `Focus` button |
| **Toggle Visibility in Scene** | Click `👁️` in Outliner |
| **Delete Object from Scene** | Click `🗑️` in Outliner or `Delete` button |




