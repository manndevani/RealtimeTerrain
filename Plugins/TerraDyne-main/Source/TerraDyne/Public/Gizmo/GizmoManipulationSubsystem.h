// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gizmo/GizmoTypes.h"
#include "GizmoManipulationSubsystem.generated.h"

class ARuntimeGizmoActor;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGizmoManipulationBegin, USceneComponent* /*Target*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGizmoManipulationEnd, USceneComponent* /*Target*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGizmoManipulationCancel, USceneComponent* /*Target*/);

/**
 * The single owner of the runtime gizmo's live state and its one spawned ARuntimeGizmoActor --
 * building-editor and road-point-editor controllers both drive this instead of each keeping their
 * own bespoke raycast/plane-intersection/delta-apply code (see the class comment on
 * RuntimeGizmoActor.h and the plan this was built from).
 *
 * Hit-testing reads the live ARuntimeGizmoActor's own component transforms directly rather than
 * re-deriving axis/plane geometry from GizmoManipulationMath's pure resolve functions -- the
 * gizmo actor's transform is kept in sync with the current target/space/pivot every time it could
 * have changed (UpdateGizmoTransform), so hit-testing against its live transform can never disagree
 * with what is actually rendered. The delta MATH itself (ComputeTranslateDelta etc.) still takes
 * Space/TargetRotation as explicit parameters and stays exactly as pure/tested as before -- this
 * subsystem's only job is to feed those already-tested functions the right values and keep the
 * visual actor in sync, not to duplicate their logic.
 *
 * Undo integration is delegate-based, not Unreal's FScopedTransaction -- that mechanism is
 * editor-only (bound to GEditor's transaction buffer) and would not survive a packaged build, which
 * this project's own gizmo material fallback chain has already hit and documented once before
 * (TerraDyneGizmoActor.cpp). A calling controller binds its own undo-snapshot recording to
 * OnManipulationBegin/OnManipulationEnd/OnManipulationCancel instead, keeping whatever undo
 * representation actually fits its own data (a whole-transform snapshot for a building actor; a
 * point-list snapshot for a road, mirroring URealtimeRoadBuilderComponent's existing local stack).
 */
UCLASS()
class TERRADYNE_API UGizmoManipulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/**
	 * Selects NewTarget, spawning/showing the gizmo actor at its pivot if not already present.
	 * Binds the obvious default transform accessors (NewTarget's owning actor's whole transform) --
	 * correct for a target that really is "one actor to move/rotate/scale." Pass nullptr to clear
	 * (equivalent to ClearTarget).
	 */
	void SetTarget(USceneComponent* NewTarget);

	/**
	 * The general form SetTarget is a convenience wrapper around: supply your own read/write
	 * functions instead of assuming "the target's owning actor's whole transform" is the right
	 * thing to read and mutate. NominalTarget is identity-only (GetTarget()/the delegates' payload);
	 * it does not have to be the actual thing GetTransform/SetTransform touch.
	 */
	void SetTargetTransformAccessors(FGizmoGetTransform GetTransform, FGizmoSetTransform SetTransform, USceneComponent* NominalTarget);

	void ClearTarget();
	USceneComponent* GetTarget() const { return State.Target.Get(); }

	void SetMode(EGizmoMode NewMode);
	EGizmoMode GetMode() const { return State.Mode; }

	void SetCoordinateSpace(EGizmoCoordinateSpace NewSpace);
	EGizmoCoordinateSpace GetCoordinateSpace() const { return State.Space; }

	void SetPivotMode(EGizmoPivotMode NewPivotMode, const FVector& InCustomPivot = FVector::ZeroVector);
	EGizmoPivotMode GetPivotMode() const { return State.PivotMode; }

	void SetSnappingEnabled(bool bEnabled) { State.bSnappingEnabled = bEnabled; }
	bool IsSnappingEnabled() const { return State.bSnappingEnabled; }
	void SetTranslationSnap(float GridSize) { State.TranslateSnapSize = GridSize; }
	void SetRotationSnap(float AngleDegrees) { State.RotateSnapDegrees = AngleDegrees; }
	void SetScaleSnap(float Increment) { State.ScaleSnapIncrement = Increment; }

	/**
	 * Hover-only: updates the highlighted handle without starting a drag. No-op (returns None)
	 * while a manipulation is already in progress, so hovering elsewhere mid-drag can't steal the
	 * highlight from the handle actually being dragged.
	 */
	EGizmoHandleType UpdateHover(const FGizmoInteractionRay& Ray, const FVector& CameraForward);

	/** Starts a drag if Ray hits a handle of the current mode within its click tolerance. Returns the handle grabbed, or None if nothing was close enough (in which case no manipulation was started). */
	EGizmoHandleType BeginManipulation(const FGizmoInteractionRay& Ray, const FVector& CameraForward);

	/** Continues an in-progress drag. No-op if not currently manipulating. */
	void UpdateManipulation(const FGizmoInteractionRay& Ray, const FVector& CameraForward);

	/** Commits the current drag's transform and fires OnManipulationEnd. No-op if not currently manipulating. */
	void EndManipulation();

	/** Restores the target's transform to exactly its value at BeginManipulation and fires OnManipulationCancel. No-op if not currently manipulating. */
	void CancelManipulation();

	bool IsManipulating() const { return State.bIsManipulating; }
	EGizmoHandleType GetActiveHandle() const { return State.ActiveHandle; }

	/** The shared gizmo actor itself, or null if none has been spawned yet -- for callers that need to e.g. add it to a separate trace's ignore list. */
	AActor* GetGizmoActor() const;

	FOnGizmoManipulationBegin OnManipulationBegin;
	FOnGizmoManipulationEnd OnManipulationEnd;
	FOnGizmoManipulationCancel OnManipulationCancel;

private:
	ARuntimeGizmoActor* EnsureGizmoActor();
	void UpdateGizmoTransform();
	FVector GetPivotLocation() const;

	/** Finds the closest handle (of the current mode) within its own click tolerance; OutHitPoint is the world-space point that hit was found at (used to seed BeginManipulation's InitialIntersection). */
	EGizmoHandleType FindHoveredHandle(const FGizmoInteractionRay& Ray, const FVector& CameraForward, FVector& OutHitPoint) const;

	UPROPERTY()
	TObjectPtr<ARuntimeGizmoActor> GizmoActor;

	FGizmoManipulationState State;
};
