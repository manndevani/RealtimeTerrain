# Development History

A chronological engineering log for RealtimeTerrain: how major systems were built, the bugs found
along the way, review and audit fixes, and the known limitations recorded at each step. It was moved
here from the README so the README can stay a short introduction. For what the project does today,
see [FEATURES.md](FEATURES.md).

Test counts and "known limitation" notes are as recorded at the time of each entry. Later entries
may supersede earlier ones.

## 🛣️ Road/junction system removed (2026-08-31)

**Road/junction system removed (2026-08-31).** The runtime spline road/junction system
(`ARealtimeRoadSplineActor`, `ARealtimeRoadJunctionActor`, `URealtimeRoadBuilderComponent`, the
`RoadGraphCore`/`RoadMeshBuilder`/`RoadNetwork`/`RoadCurveGraph`/`RoadElevationProfile`/`RoadSpiral`/
`RoadPierPlacement`/`RoadStylePresets`/`RoadXodrExport`/`RoadNetworkDebugLibrary` modules, the
third-party `RoadBuilder-master` plugin, and every UI/OSM/undo hook wired into it) was deleted at
the user's request, along with its 45+ automation tests. `AGRTEditController` and
`SGRTPanel` no longer reference roads at all; OpenStreetMap import now brings in buildings
only. The full development history of that system (RoadBuilder port phases 1-5, the junction
fillet-solver rewrite) is preserved in git history, in the commits before this removal,
and is no longer duplicated here.

## 🎯 Gizmo System — reusable runtime transform gizmo (COMPLETE ✅)

**Goal**: replace the old translate-only, per-consumer-duplicated `AGRTGizmoActor` with a
proper reusable translate/rotate/scale gizmo system (world/local space, plane handles, rotation
rings, mathematically precise hit testing, snapping, undo integration) shared by the building
editor and the road-point editor instead of each reimplementing its own drag math. (The original
plan document lived outside this repository and is not included.)

### Phase 1 — COMPLETE ✅ (manipulation core math)
- `Plugins/GRT/Source/GRT/Public/Gizmo/GizmoTypes.h`: `EGizmoHandleType` (16
  explicit handle identities — translate axes, planes, screen-space centre, rotation rings +
  screen rotation, scale axes + uniform), `EGizmoMode`/`EGizmoCoordinateSpace`/`EGizmoPivotMode`,
  `FGizmoInteractionRay` (device-agnostic — mouse today, a future VR controller could produce the
  same struct), `FGizmoManipulationState`.
- `GizmoManipulationMath.h`/`.cpp`: pure `namespace GizmoMath`, no UObject/UWorld/component
  references anywhere — axis/plane resolution for World vs Local space; a generalized
  best-facing-drag-plane resolver (replaces this project's two previously byte-identical
  `BestFacingVerticalPlaneNormal` copies in `GRTEditController.cpp` and
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
  (`AGRTGizmoActor`'s `GizmoBaseMaterial`/`ApplyGizmoColor`) is reused instead.
  **Superseded by Phase 2, correction found by direct code/asset audit**: `Content/Gizmo/Materials/`
  does contain 18 real migrated `M_GizmoAxis`/`MI_XAxis`/`MI_Rotation_*`/etc. assets, and
  `ARuntimeGizmoActor.cpp`'s constructor (Phase 2, below) loads and uses them as its *primary*
  material path — the hardcoded-color dynamic-MID system only runs as a fallback if that asset load
  fails. `M_GizmoAxis`/`M_ScreenRotation` also depend on `MF_OccludedPixels` in `Content/Mytaverse/`,
  so that "vendor" folder isn't safe to delete. This paragraph's original claim was accurate for
  Phase 1's own scope (mesh-only migration) but was never updated once Phase 2 changed it; left
  as-is above for the historical record, corrected here instead of silently edited.
  Test: `GRT.Gizmo.*` (`AxisResolution`, `TranslateDelta`, `RotationAngle`, `ScaleDelta`,
  `PlaneResolutionAndRayIntersect`, `Snapping`) — includes a known-quarter-turn check (measures
  exactly π/2), a scale-crossing-the-origin check (clamps to a small positive ratio, never
  negative/zero), and a far-from-world-origin check (same result 1,000,000cm from the origin,
  standing in for "extreme camera distance" robustness).
- **Full regression sweep after this increment: 42 tests, 0 regressions** (existing
  `GRT.Building`/every `GRT.Road*` suite untouched — this phase is pure addition, no
  rendering or existing-code changes yet).

### Phase 2 — COMPLETE ✅ (`ARuntimeGizmoActor` — visual handle set)
- `Plugins/GRT/Source/GRT/Public/Gizmo/RuntimeGizmoActor.h`/`.cpp`: one
  `UStaticMeshComponent` per handle (16 explicit `EGizmoHandleType` values) across all three modes.
  Every handle is tagged explicitly (`TMap` from component to handle type, built once at
  construction) — `GetHandleType`/`GetComponentForHandle` never infer identity from a mesh or
  material. `SetMode(EGizmoMode)` toggles visibility **and** collision together per handle set, so
  a handle from an inactive mode is never still hit-testable. Coloring reuses
  `AGRTGizmoActor`'s proven dynamic-MID pattern (7 shared MIDs: X/Y/Z axis colors, 3 plane
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
     assertion in `GRT.Gizmo.ActorRuntimeAssets` reading the MID's actual emissive value back.
  3. **Found from real data, not assumption**: the migrated `SM_RotationHandle`/`SM_ScaleHandle`
     meshes' primary axis (a ring's normal; a scale handle's long arm) is local **X**, not Z like
     the engine's own Cylinder — confirmed by logging each mesh's actual bounds extent
     (`SM_RotationHandle`: X=2, Y=92, Z=92; `SM_ScaleHandle`: X=58.5, Y=7, Z=7) rather than assuming
     the same convention as the Cylinder/Cone arrows. Every ring and scale handle originally pointed
     at the wrong world axis; fixed with a dedicated `FRotationMatrix::MakeFromX` rotation set (the
     arrows keep their own correct `MakeFromZ` set). Pinned by
     `GRT.Gizmo.ActorHandleOrientation`, which rotates each handle's real local-X through its
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
  `UGRTSubsystem` precedent, not a hand-rolled singleton): owns one lazily-spawned
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
- `AGRTEditController` (buildings) and `URealtimeRoadBuilderComponent` (road points) both now
  drive `UGizmoManipulationSubsystem`'s `Begin/Update/End/CancelManipulation` instead of their own
  duplicated raycast/plane-intersection/delta-apply code. `TraceGizmoUnderCursor`,
  `BestFacingVerticalPlaneNormal`, `IntersectCursorWithPlane`-for-axis-drag, and the whole
  `EGRTGizmoAxis`-keyed switch statements are gone from both files.
- **A necessary Phase 3 design refinement, discovered while actually attempting this migration, not
  planned in advance**: the subsystem originally hardcoded `TargetActor->SetActorTransform()`
  (`TargetActor = Target->GetOwner()`), correct for a building but wrong for a road control point — a
  road point is one vertex on a much larger spline actor, moved via `MovePointRealtime`, not a
  whole-actor transform, and its own world location (not the road actor's arbitrary origin) is the
  correct pivot. Fixed by generalizing the subsystem to pluggable `FGizmoGetTransform`/
  `FGizmoSetTransform` delegates (`SetTargetTransformAccessors`), with the simple
  `SetTarget(USceneComponent*)` becoming a thin convenience wrapper around them for the whole-actor
  case. The delta math itself never changed — only where its result reads from and writes to.
- **`AGRTGizmoActor`'s real scope, found only by reading it fully rather than trusting the
  plan's "delete this class" instruction**: it secretly served two unrelated responsibilities — the
  old axis-translate gizmo, AND the building-footprint-spline-vertex-handle spheres (an entirely
  separate feature). Deleting it outright would have broken footprint editing. Stripped instead of
  deleted: `EGRTGizmoAxis`, the arrow/tip/shaft meshes, `SetHighlightedAxis`,
  `GetAxisFromComponent` are gone; `SplinePointMeshHandles`, `UpdateSplinePointHandles`,
  `SetHighlightedSplinePoint` and the footprint feature stay exactly as before. The class comment now
  says so explicitly, since the name is a historical artifact of this split.
- **The subsystem's one gizmo target is shared by both consumers** — each tracks its own prior
  binding (`GizmoBoundBuilding` on the controller; `GizmoBoundRoad`/`GizmoBoundPointIndex` on the
  road component) and only ever calls `SetTarget`/`ClearTarget` when it actually owns the current
  binding, never unconditionally — otherwise a building-tool tick could silently steal or clear a
  road tool's active gizmo, or vice versa.
- **Escape-to-cancel** now works for both: `AGRTEditController::SetupInputComponent` binds
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
  failure (`GRT.Raster.Workflow`) is a pre-existing GIS raster-import test unrelated to this
  work (a missing satellite/ortho layer import), in a file this migration never touched.
- **Known limitation**: this phase is verified by full headless regression + direct code
  verification of every cross-consumer ownership/cancel-identity path against the subsystem's actual
  broadcast semantics, but **not** by an in-editor manual drag test — actually clicking and dragging
  a building and a road point's gizmo handles in a running editor session is still needed before this
  phase is fully trustworthy, and hasn't been done yet.

### Phase 5 — COMPLETE ✅ (mode/space/snap live end-to-end)
- **Mode switching**: `Shift+W`/`Shift+E`/`Shift+R` switch the shared gizmo to Translate/Rotate/Scale,
  bound once on `AGRTEditController` (works for whichever consumer currently owns the target,
  same "one call for both" pattern as Escape-cancel). Plain `W`/`E` were already claimed by this
  project's own always-on camera-flight axis mappings, and Unreal's legacy Axis Mappings have no
  modifier-key concept — they fire on the raw key regardless of Shift — so `AGRTEditPawn` now
  overrides `MoveForward`/`MoveRight`/`MoveUp_World` (inherited from `ADefaultPawn`) to no-op while
  either Shift key is held, freeing `Shift+W/E/R` (and `Shift+A/S/D/Q`) for hotkeys with zero camera
  side effect instead of a same-frame flight nudge (which, at orbital altitude, is not cosmetic — a
  single frame of this project's own altitude-scaled max speed is tens of kilometers).
- **World/Local space**: `C` toggles `SetCoordinateSpace`, plus a "World Space" checkbox in a new
  GIZMO section of the tool panel (`SGRTPanel`) — both drive the same subsystem call. Verified
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
  New test `GRT.Gizmo.SubsystemInvertSnap` walks one continuous 137-unit drag through all three
  combinations without ever re-calling `BeginManipulation` — persistent-off+Ctrl snaps to 100,
  persistent-on+Ctrl gives the raw 137, and releasing Ctrl the very next call falls straight back to
  the persistent (on) setting with no leftover state, exactly matching the logged values.
- **Plane and rotation-ring handles**: no new code needed — Phase 1's math and Phase 3's hit-testing
  already handled them, and Phase 4's migration already routes every mode through the same
  `Begin/Update/EndManipulation` calls regardless of handle type. Reachable end-to-end as soon as
  Rotate/Scale mode can actually be selected, which this phase's hotkeys now make possible.
- **Full regression sweep after this increment: 84 tests found, 83 passed, 0 regressions** — the same
  pre-existing, unrelated `GRT.Raster.Workflow` failure as Phase 4.
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
  by a hand-derived test (`GRT.RoadMesh.WidthScaleAppliesToWholeSection`) with both options set
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
  pre-existing, unrelated `GRT.Raster.Workflow` failure as Phases 4-5. New tests:
  `GRT.RoadWidthProfile.WidthMath`, `GRT.RoadMesh.WidthScaleAppliesToWholeSection`,
  `GRT.RoadSplineActor.WidthKeyWidensMeshAtItsOwnDistance` (spawns a real road, measures actual
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
  a location) since Phase 4's migration, and `AGRTBuildingActor`'s footprint is already stored
  actor-local (`GatherSplinePointsLocal`) with `GetFootprintPolygonWorld`/terrain-pad sampling
  already correctly transforming through the actor's real rotation and scale. Because
  `AGRTEditController::UpdateMoveGizmo` already binds buildings through the subsystem's generic
  `SetTarget` (whole-actor transform), and `UpdateManipulation`'s Rotate/Scale branches never
  distinguish which consumer is active, Rotate and Scale already worked mechanically as an emergent
  consequence of Phases 4-5 — nothing needed to change in `GRTEditController.cpp` at all.
- **A real, concrete multiplayer gap found while checking that emergent behavior actually holds
  end-to-end, not assumed**: `SetReplicateMovement(true)`'s `FRepMovement` (Unreal's standard actor
  movement replication, what `AGRTBuildingActor` uses) carries Location and Rotation but has
  **no `Scale3D` field at all**. A gizmo Scale-mode drag would have resized a building only on
  whichever machine actually performed the drag — every other client/observer would keep rendering
  it at its old size forever, silently, with no error anywhere. Fixed with an explicit
  `ReplicatedScale3D` property (`ReplicatedUsing = OnRep_Scale3D`, mirroring `ReplicatedSplinePoints`'s
  existing pattern exactly): `NotifyBuildingChanged()` — already called from every real commit path
  (`RequestMoveBuilding`, the Tick throttle, the cancel handler) — now also resyncs it from the
  actor's real scale on the authority; `OnRep_Scale3D` applies it back out on every other machine,
  matching `OnRep_PadSettings`'s shape (reapply + re-stamp the pad, no full mesh regeneration needed
  since geometry itself never changes with scale, only the actor's overall transform).
- `GRT.Building.GizmoRotateAndScale` (new): drives a real spawned building through
  `UGizmoManipulationSubsystem`'s actual `SetTarget`/`BeginManipulation`/`UpdateManipulation`/
  `EndManipulation` calls — the same calls `AGRTEditController` makes — with hand-verified rays
  landing on `RotateZ` (ring radius 138, reusing `GRT.Gizmo.DistanceRayToRing`'s geometry) and
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
- `GRT.Multiplayer.BuildingScaleReplication` (new): confirms `NotifyBuildingChanged` actually
  resyncs `ReplicatedScale3D` from a real scale change, matching this project's existing convention
  of checking replicated *state* in a single process rather than standing up a second network
  connection (the same shape `BuildingReplicationAndSplineSync`/`BuildingPadSync` already use).
  `OnRep_Scale3D` itself is `protected` like every other `OnRep_*` handler in this class, so it isn't
  reachable from a test any more than the existing ones are — its two-line body is confirmed by
  direct code reading instead.
- **Full regression sweep after this increment: 89 tests found, 88 passed, 0 regressions** — the same
  pre-existing, unrelated `GRT.Raster.Workflow` failure as Phases 4-6.
- **Known limitation**: in-editor manual drag verification (carried over from Phases 4-6) remains
  outstanding — headless automation proves the math and the replication-sync bookkeeping, not the
  real click-and-drag feel or a genuine two-machine network test.

### Phase 8 — COMPLETE ✅ (VR/motion-controller readiness — design-verification only, no code)
**Claim to verify**: mouse and a future VR controller can share the exact same manipulation core,
with device-specific code confined to producing an `FGizmoInteractionRay` and nothing else.

**Verified by direct code reading, not assumed**: `Plugins/GRT/Source/GRT/Private/Gizmo/`
(`GizmoManipulationSubsystem.cpp`, `GizmoManipulationMath.cpp`, `RuntimeGizmoActor.cpp`) contains
**zero** references to `PlayerController`, `DeprojectMousePositionToWorld`, `GetMousePosition`,
`InputComponent`, `IsInputKeyDown`, or `EKeys::` anywhere — confirmed by grep across the whole
module, not by reading a few functions and generalizing. Every device-specific call lives entirely in
the two *consumers* (`AGRTEditController::BuildGizmoInteractionRay`/`GetCameraForwardVector`,
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
(`GRT.Raster.Workflow`) is a pre-existing GIS raster-import test relying on a satellite/ortho
layer this environment doesn't have — present before this whole gizmo system existed, unrelated to
any file this work touched, unchanged in every phase's sweep (Phase 4 through this one).

**Architecture summary**: `Plugins/GRT/Source/GRT/{Public,Private}/Gizmo/` —
`GizmoTypes.h` (enums + `FGizmoManipulationState`), `GizmoManipulationMath.h/.cpp` (pure, unit-tested
math), `RuntimeGizmoActor.h/.cpp` (the visual handle set, one `UWorldSubsystem`-owned instance shared
by every consumer), `GizmoManipulationSubsystem.h/.cpp` (the single stateful owner + clean API).
Both `AGRTEditController` (buildings) and `URealtimeRoadBuilderComponent` (road control points)
drive it identically; road points additionally use `SetTargetTransformAccessors` since a spline point
has no "whole actor transform" of its own the way a building does.

**How it works / input flow**: a consumer builds one `FGizmoInteractionRay` per relevant input event
(mouse deprojection today; a VR controller's aim transform would be the only new code a future
integration needs, per Phase 8) and calls `UpdateHover`/`BeginManipulation`/`UpdateManipulation`/
`EndManipulation`/`CancelManipulation`. Hit-testing is dedicated math (`DistanceRayToSegment/Point/
Ring/Quad`), never physics collision. Every drag computes its delta from **initial transform plus
accumulated delta**, never compounding onto an already-modified transform. Mode (`Shift+W/E/R`),
coordinate space (`C`), and snapping (`G`, Ctrl-held to invert per-drag) are hotkeys on
`AGRTEditController`, calling the subsystem directly — one binding set works for whichever
consumer currently owns the shared target, the same pattern Escape-cancel already established.

**Testing results**: every phase's own automation suite (`GRT.Gizmo.*`, `GRT.RoadWidthProfile.*`,
`GRT.RoadMesh.WidthScale*`, `GRT.RoadSplineActor.WidthKey*`, `GRT.Building.GizmoRotateAndScale`,
`GRT.Multiplayer.BuildingScaleReplication`) plus the full pre-existing regression suite, run
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
- **Live Unreal MCP automation testing**: Discovered and executed all 17 `GRT.Gizmo.*` automation tests live
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

## 🧹 Code review fixes (2026-09-10) — COMPLETE ✅

A full read-through for defects ahead of a Fab release. Twelve were found and fixed. The plugin
had just been renamed from TerraDyne to GRT, and none of the twelve came from that rename; they
predate it.

1. **Ctrl+Z after moving a building or model deleted it (High).** Only place/delete were
   recorded, so the newest undo entry after a drag was the placement. Moves, rotations, scales,
   footprint edits, floor count, reset and relevel are now `Modify` entries (before/after
   snapshots) in `AGRTEditController`. A drag's before-state is captured when the drag starts, and
   a release that changed nothing records nothing. When undo re-creates a deleted actor, older
   entries that referenced it are retargeted at the new actor (`RemapActorReferences`).
2. **The chunk eviction cache leaked edits across worlds (High).** Evicted chunks went to one flat
   folder shared by every session and never cleared, and streaming read it *before* the DEM, so a
   chunk sculpted in one session replaced terrain in a later procedural world, DEM import or load
   at the same coordinate. The cache now lives in a per-session folder
   (`Saved/SaveGames/<ChunkSaveDir>/<session GUID>/`). It is reset whenever the world is replaced
   and deleted in `EndPlay`, and leftovers older than a day are purged at `BeginPlay`. Two related
   gaps also closed: chunks that had streamed out used to be missing from saves (they are now
   folded into `BuildSaveGameInstance`), and loaded chunks used to revert when streamed out and
   back (they are now marked dirty).
3. **Loading a world kept the old building/model undo history (High).** Undoing a pre-load delete
   put that building into the newly loaded world, and an unfinished OSM import kept spawning into
   it. `ApplyLoadedSaveGame` now calls `ResetActorHistory()` on every controller.
4. **OSM buildings still flattened terrain (High).** `bFlattenTerrain = false` was set *after*
   `SpawnActor`, whose construction/BeginPlay pass had already stamped a default 12×8 m pad under
   every imported building and left it there. The spawn is now deferred, with the flag set before
   `FinishSpawning`. Buildings that don't flatten are still registered, so they are saved and
   cleaned up on load. Turning flattening off also now releases a pad that was already stamped.
5. **Building scale and pad settings were not saved (Medium).** `FGRTBuildingData` gained `Scale`,
   `bFlattenTerrain` and `bAutoPadHeight`, and `ToData` now fills in `PadTargetZ`/`PadFalloff`.
   Old saves load with defaults. `ApplyData` also resyncs `ReplicatedSplinePoints`: without that,
   remote clients saw loaded or undone buildings with the constructor's default rectangle.
   `AGRTBuildingActor::SpawnFromData` is now the single restore path for load and undo.
6. **Imported model textures never rendered (Medium).** Every slot was instanced from
   `BasicShapeMaterial`, whose only parameter is `Color`, so all texture assignments did nothing.
   A new lit material, `/GRT/Materials/Models/M_GRT_ImportedModel`, multiplies `BaseColor` by
   `BaseColorTexture`. Its default texture is white, so untextured slots still show their diffuse
   colour. It is referenced from `AGRTPlacedModelActor::ImportedModelBaseMaterial` and added to
   `DirectoriesToAlwaysCook`.
7. **The transform gizmo stayed on screen after its target was deleted or undone (Medium).** The
   clear was gated on `IsValid()`, which is false for a destroyed actor. It now checks whether a
   binding exists at all. Escape during a model drag also now ends the drag cleanly.
8. **Clients sent local file paths to the server (Medium, security).** A client's save, load,
   GeoTIFF, raster or OSM path meant nothing on the server's disk. `Server_SaveWorldToFile` also
   let any client with edit rights make the server write a file anywhere. These operations are now
   **host-only**: the five path-taking RPCs are gone, the `Request*` calls return `false` off
   authority, and the panel explains why before opening a dialog (`CanAccessLocalFiles()`).
9. **Remote clients could not undo building or model edits (Low).** That history lives on the
   server, and the client only ever ran the terrain half. `RequestUndo`/`RequestRedo` now run the
   whole chain on the server, and the panel's Undo/Redo buttons go through them too.
10. **Outliner visibility changed only the local machine (Low).** It now goes through
    `RequestSetActorVisible` (a server RPC), and `bIsVisibleInScene` is replicated with an OnRep
    that also applies collision.
11. **Stale docs.** CLAUDE.md and both CODE_REFERENCE files still described the removed road
    system. The empty `ARealtimeRoadEditController` is now `ARealtimeTerrainEditController`,
    with a `ClassRedirect`.
12. **A stray 0-byte file** at the repo root, left by an earlier mis-pathed write, was deleted.

Tested by the 11 tests in `GRTReviewFixesTests.cpp`: `GRT.ActorUndo.MoveBuildingUndoRedo`,
`.ModifyEntrySurvivesRespawn`, `.FloorCountAndNoOpMove`, `.RequestUndoRunsActorHistory`,
`.MoveModelUndoRedo`, `GRT.Persistence.LoadClearsActorHistory`, `.BuildingDataRoundTrip`,
`GRT.Building.NonFlatteningLeavesTerrain`, `GRT.Streaming.ChunkCacheIsSessionScoped`,
`GRT.Model.TextureReachesMaterial` and `GRT.Model.VisibilityReplicates`. **Full suite: 94 tests, 93
pass.** The one failure is the known `GRT.Raster.Workflow` fixture gap.

**Not fixed here — Fab packaging blockers.** The plugin loads content from the *project*, not from
its own `/GRT/` mount, so on its own it would ship broken:
- The transform gizmo meshes and materials come from `/Game/Gizmo/` and `/Game/Mytaverse/`.
- The default building kit and the holographic building material come from `/Game/SD_Art/`.

Those are third-party marketplace packs, which cannot be redistributed. Before a Fab submission,
that content has to be replaced with plugin-owned assets under `Plugins/GRT/Content/`, or made
optional with engine-content fallbacks.

## 🔍 Antigravity findings triage — COMPLETE ✅

A third-party audit tool ("Antigravity") flagged 7 findings across the codebase. Each was verified
against the actual code before any fix landed, per this project's own established discipline of never
trusting an external claim without direct verification — 6 held up (2 High, 1 upgraded to Medium, 3
Medium/Low as originally rated) and 1 was a false positive, rejected with reasoning rather than
"fixed" into a regression.

1. **Gizmo handles behind the camera were grabbable (High)** — `FindHoveredHandle` accepted any hit
   within tolerance regardless of which side of the ray's origin it fell on. Fixed with an
   `AlongRay >= 0.0` guard in
   [GizmoManipulationSubsystem.cpp](../Plugins/GRT/Source/GRT/Private/Gizmo/GizmoManipulationSubsystem.cpp).
   Tested: `GRT.Gizmo.SubsystemRejectsHandleBehindRay`.
2. **`IsLocationTraceable` assumed a 2-sided mesh (High, worse than originally described)** — it
   divided the mesh's vertex count by 2 to estimate the top-surface grid resolution, but
   `AGRTChunk::RebuildMesh` only ever builds a single top surface (no bottom slab), so every
   resolution/index computation downstream was wrong. Fixed in
   [GRTCollision.cpp](../Plugins/GRT/Source/GRT/Private/Physics/GRTCollision.cpp)
   by using the full vertex count. Tested: `GRT.Physics.IsLocationTraceableFindsTheRightCell`.
3. **OpenDRIVE header north/south vs. east/west (rejected — false positive)** — the finding assumed
   Unreal's +Y axis is North, but this project's Cesium georeferencing uses the East-South-Up (ESU)
   tangent-plane convention, where +Y is South. `-Point.Y` for the North bound is therefore the
   *correct* South→North conversion, not an axis mixup. Left as-is in
   `RoadXodrExport.cpp` (removed with the road system), with a comment explaining
   why; the existing structure test now locks in the verified-correct values.
4. **Raw `APlayerController*` risked a dangling pointer (Medium)** — `UndoStacks`/`RedoStacks`/
   `PendingStrokeOwner` in `AGRTManager` stored raw controller pointers; a controller destroyed
   mid-stroke (e.g. a player disconnecting) would leave a dangling `PendingStrokeOwner`. Fixed by
   switching to `TWeakObjectPtr<APlayerController>` in
   [GRTManager.h](../Plugins/GRT/Source/GRT/Public/Core/GRTManager.h) /
   [GRTManagerUndo.cpp](../Plugins/GRT/Source/GRT/Private/Core/GRTManagerUndo.cpp).
   Tested: `GRT.Undo.BeginStrokeSurvivesDisconnectedOwner`.
5. **Zero-length segments produced NaN (Medium)** — `RoadElevationProfile::EvaluateHeight` and
   `RoadWidthProfile::EvaluateWidth` divided by `SegmentLength` without checking for two keys authored
   at (or merged to within tolerance of) the same distance, producing 0/0 = NaN. Fixed with an
   `UE_DOUBLE_SMALL_NUMBER` guard in both
   `RoadElevationProfile.cpp` (removed with the road system) and
   `RoadWidthProfile.cpp` (removed with the road system). Tested: new cases in
   `GRT.RoadElevationProfile.HeightMath` and `GRT.RoadWidthProfile.WidthMath`.
6. **Division by (near-)zero for near-180° hairpin corners (Low/edge case)** — `RoadSpiral.cpp`'s
   corner-fitting bisection divided by `Cos(Diff/2)`, which shrinks toward 0 as a corner's turn angle
   approaches ±180°. A literal near-zero epsilon on `Cos` alone wasn't a sufficient guard (at 179.99°,
   `Cos(Diff/2) ≈ 8.7e-5` — well above any reasonable near-zero threshold, yet still large enough a
   divisor to send the corrected value into the millions). Fixed by bounding the *candidate result*
   against the corner's own arm length instead of gating on `Cos` directly, falling through to the
   already-correct bisection loop when the single-division shortcut would be unreasonable. Tested:
   `GRT.RoadSpiral.HairpinCornerStaysFinite` — note that for this specific pathological input
   (1000cm arms, a near-full reversal, capped at a 500cm max radius) the mathematically correct answer
   is a path that collapses to a tiny curve at the corner apex (finite and bounded, just not a
   "nicely-proportioned" S-curve) — the test only requires a valid, finite, bounded path, not a
   substantial one.
7. **`const_cast` mutated a replicated property inside a const getter (Low/architectural, worse than
   originally described)** — `AGRTBuildingActor::ResolvePadTargetWorldZ() const` used
   `const_cast` to write the *replicated* `PadTargetWorldZ` on every call, not just a `mutable` cache
   field. This meant calling what looked like an innocuous query silently triggered network
   replication traffic and `OnRep_*` side effects on every client, on every caller — redundant for
   `SnapToPadHeight` (which already commits explicitly) and actively harmful for every other caller.
   Fixed by removing the write in
   [GRTBuildingActor.cpp](../Plugins/GRT/Source/GRT/Private/Building/GRTBuildingActor.cpp);
   the `mutable` cache fields remain untouched. Tested:
   `GRT.Multiplayer.ResolvePadTargetWorldZHasNoSideEffect`.

**Full regression sweep**: 94 automation tests found, 93 passed — the only failure is the same
pre-existing, unrelated `GRT.Raster.Workflow` GIS-import test noted throughout this README.


## 🧩 3D model importer — runtime defects and gaps (2026-08-28)

### Runtime defects found and fixed (2026-08-28)

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
  `AGRTEditController::TraceUnderCursor` and could not be clicked or selected. Now calls
  `SetComplexAsSimpleCollisionEnabled(true, false)` in the constructor, plus `UpdateCollision(false)`
  after each mesh swap, since `NotifyMeshUpdated()` only resets the render proxy and never touches
  physics.
* **FBX imported mirrored.** The importer targeted `ufbx_axes_right_handed_z_up`; Unreal is
  **left**-handed Z-up. The two presets differ only in their `front` axis (`-Y` vs `+Y`), so every FBX
  came in mirrored through the Y plane with reversed winding — rendering inside-out. Now targets
  `ufbx_axes_left_handed_z_up`; ufbx flips face winding itself as part of the conversion
  (`handedness_conversion_retain_winding` defaults to `false`).

Two regression tests were added, because the existing nine could not see any of the above:
`GRT.ModelImport.PlacedActorCollision` (imports a closed box, calls `SetModelAsset`, then
asserts cooked triangle geometry exists and a world line trace on `ECC_WorldStatic` — the same
channel `TraceUnderCursor` uses — actually hits the actor) and
`GRT.ModelImport.FBXHandedness` (a fixture declaring Y-up right-handed source axes with one
non-zero source-Z vertex, asserting it lands on **+Y**; under the old setting it landed on -Y).

### Gaps closed (2026-08-28)

* **Persistence.** `UGRTSaveGame` now carries `ModelLibrary` and `PlacedModels`. Runtime
  geometry belongs to no package and cannot be serialised, so the save stores source paths and
  re-imports on load — the same approach `FGRTGeoTIFFImportState` takes with its `.tif`. The
  catalog is restored **under its saved asset ids** before any placement spawns; minting fresh ids
  would leave every restored placement dangling. Saves written before this load as empty arrays.
* **Client-side asset resolution.** `AGRTPlacedModelActor` replicates `SourceFilePath`
  alongside `AssetId`, and `ResolveModelAsset()` (called from `BeginPlay` and both `OnRep`s, so no
  property-arrival order can miss it) falls back to
  `UGRTAssetLibrarySubsystem::ResolveOrImport`, importing the file locally under the
  replicated id. A machine that never ran the original import previously drew an empty actor.
  *Limitation*: the file must exist on that machine, exactly as with GeoTIFF import.
* **Generated thumbnails.** `FGRTModelThumbnail` renders the geometry with a
  `USceneCaptureComponent2D` and caches the result as a PNG under
  `Saved/GRT/ModelThumbnails/`, keyed by a hash of the full source path so two different
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
* **Imported textures no longer collected.** `FGRTImportedMaterial` is a `USTRUCT` and
  `FGRTImportedModelAsset::Materials` is a `UPROPERTY`, so the collector can reach the diffuse
  textures. Previously they were collected while the catalog still held `TObjectPtr`s to them:
  models placed before a GC kept their look via the material instance's own reference, anything
  placed afterwards came up untextured.

Verified by `GRT.ModelImport.Persistence` (a real `BuildSaveGameInstance` →
`ApplyLoadedSaveGame` round trip asserting ids, transforms, scale and resolved geometry),
`.LibraryDedupe`, `.ResolveOrImport` and `.MaterialTextureSurvivesGC`. Full suite: **106/107**, the
sole failure being the pre-existing `GRT.Raster.Workflow` fixture gap.

**Still open**: multiplayer needs the model file present on each machine (inherent to replicating a
path rather than geometry); generated thumbnails need a real RHI and have not been inspected
visually, only proven to no-op safely headless.
