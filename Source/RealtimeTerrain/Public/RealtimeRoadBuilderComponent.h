// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoadTypes.h"
#include "RealtimeRoadBuilderComponent.generated.h"

class ARealtimeRoadSplineActor;
class ARealtimeRoadJunctionActor;
class ATerraDyneManager;
class ARoadEditOverlayActor;
class UGizmoManipulationSubsystem;
class APlayerController;

/**
 * A road's control points as they were before an edit.
 *
 * Whole-road snapshots rather than per-operation deltas: a road is a short list of points, and
 * storing the list outright means undo cannot drift out of step with the geometry the way replaying
 * inverse operations can.
 */
USTRUCT()
struct FRoadEditSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<ARealtimeRoadSplineActor> Road;

	UPROPERTY()
	TArray<FVector> Points;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoadPlaced, ARealtimeRoadSplineActor*, PlacedRoad);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRoadPointSelected, ARealtimeRoadSplineActor*, RoadActor, int32, PointIndex);

/**
 * Runtime road placement tool.
 *
 * Put this on the player's pawn or controller. Because it is owned by a player it can reach the
 * server, which is what lets a client commit a road: the ghost the player drags is local and never
 * replicates, and only the finished route is sent to the authority to spawn for real.
 */
UCLASS(Blueprintable, ClassGroup=(Roads), meta=(BlueprintSpawnableComponent))
class REALTIMETERRAIN_API URealtimeRoadBuilderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URealtimeRoadBuilderComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Builder|Selection")
	TWeakObjectPtr<ARealtimeRoadSplineActor> SelectedRoadActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road Builder|Selection")
	int32 SelectedPointIndex = INDEX_NONE;

	/** Local, non-replicating ghost currently being drawn. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Road Builder|State")
	TObjectPtr<ARealtimeRoadSplineActor> ActiveDraftRoad;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Configuration")
	ERoadType DefaultRoadType = ERoadType::PavedRoad;

	/** Spacing in cm of the intermediate points generated when extending a road. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Generation", meta = (ClampMin = "200.0"))
	float AutoPointSpacing = 800.0f;

	/** Radius in cm within which road endpoints snap to junction sockets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Snapping", meta = (ClampMin = "50.0"))
	float JunctionSnapRadius = 600.0f;

	/** Radius in cm within which a click lands on an existing spline point instead of empty ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Snapping", meta = (ClampMin = "10.0"))
	float PointSelectionRadius = 400.0f;

	/**
	 * Radius in cm within which a click that missed every control point still counts as landing on
	 * a road's line. Clicking there inserts an anchor point rather than starting a new road.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Snapping", meta = (ClampMin = "10.0"))
	float RoadLineSnapRadius = 500.0f;

	/** Drop clicks that missed a junction onto the terrain rather than trusting the click's Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Snapping")
	bool bSnapToTerrain = true;

	//--- EDIT OVERLAY ---

	/** Draw the route line and control point handles for whatever road is selected or hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Overlay")
	bool bShowEditOverlay = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Road Builder|Overlay")
	TObjectPtr<ARoadEditOverlayActor> EditOverlay;

	/** Road whose handle is currently under the cursor. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Road Builder|Selection")
	TWeakObjectPtr<ARealtimeRoadSplineActor> HoveredRoadActor;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Road Builder|Selection")
	int32 HoveredPointIndex = INDEX_NONE;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Road Builder|State")
	bool bIsDraggingPoint = false;

	/** Show the XYZ translate gizmo on the selected control point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Overlay")
	bool bShowPointGizmo = true;

	/** Cached world subsystem driving the shared runtime gizmo -- also used by the building tool. */
	UPROPERTY(Transient)
	TObjectPtr<UGizmoManipulationSubsystem> GizmoSubsystem;

	/**
	 * Which road/point the gizmo's transform accessors are currently bound to -- rebinding a fresh
	 * pair of closures every tick regardless of whether the selection actually changed would be
	 * exactly the kind of per-frame allocation a drag-time system should avoid.
	 */
	TWeakObjectPtr<ARealtimeRoadSplineActor> GizmoBoundRoad;
	int32 GizmoBoundPointIndex = INDEX_NONE;

	/**
	 * Handles a press on the edit overlay. Call on mouse down.
	 *
	 * Selecting and dragging are deliberately separate presses. A press on an unselected handle
	 * only selects it; a second press on the same handle begins the drag. Doing both at once meant
	 * the click that picked a point also flung it, because the drag immediately moved it to
	 * wherever the cursor's ground trace landed.
	 *
	 * Returns true when the press was consumed, so the caller does not also treat it as a click on
	 * open ground.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool BeginPointDrag();

	/**
	 * Seconds between junction re-solves while a point is being dragged.
	 *
	 * Re-solving rebuilds the junction surface and rewrites road cuts, so it is throttled rather
	 * than run per frame. Small enough to look continuous, large enough not to dominate the drag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|Overlay", meta = (ClampMin = "0.0"))
	float JunctionResolveInterval = 0.08f;

	//--- EDIT HISTORY ---

	/**
	 * Records a road's current points so the next edit to it can be taken back.
	 *
	 * Call before mutating, not after. A drag records once when it begins rather than per frame,
	 * so undo steps back to where the point started rather than crawling back along its path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder|History")
	void RecordUndo(ARealtimeRoadSplineActor* Road);

	/** Takes back the last road edit. False when there is nothing to undo. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder|History")
	bool UndoRoadEdit();

	/** Reapplies the last undone road edit. False when there is nothing to redo. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder|History")
	bool RedoRoadEdit();

	/**
	 * Removes the selected control point. Once a road is down to two points, deletes the whole
	 * road instead (see DeleteSelectedRoad) rather than refusing outright.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool DeleteSelectedPoint();

	/** Removes the selected road entirely, re-solving any junction it was pinned to. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool DeleteSelectedRoad();

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool HasPointSelected() const;

	/** How many edits the history keeps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Builder|History", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxUndoSteps = 64;

	/** Ends the drag and forces the collision-bearing rebuild immediately. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void EndPointDrag();

	UPROPERTY(BlueprintAssignable, Category = "Road Builder|Events")
	FOnRoadPlaced OnRoadPlaced;

	UPROPERTY(BlueprintAssignable, Category = "Road Builder|Events")
	FOnRoadPointSelected OnRoadPointSelected;

	//--- POINT-AND-CLICK WORKFLOW ---

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void SelectRoadPoint(ARealtimeRoadSplineActor* RoadActor, int32 PointIndex);

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void ClearSelection();

	/**
	 * Main click handler.
	 *
	 * A click that lands on an existing spline point always selects that point, including while
	 * another point is already selected — that is how you move the anchor rather than being stuck
	 * extending from the first thing you ever clicked. A click on open ground with a selection
	 * extends the selected road; a click on open ground with no selection starts a new draft.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void HandleWorldClick(const FVector& ClickedWorldLocation);

	//--- CONTINUOUS DRAWING ---

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	ARealtimeRoadSplineActor* BeginRoadDraft(const FVector& StartWorldLocation, ERoadType RoadType);

	/** Drags the draft's open end. Cheap enough to call every frame. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void UpdateRoadDraft(const FVector& CursorWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void CommitPoint(const FVector& PointWorldLocation);

	/**
	 * Turns the draft into a real road. On the authority this spawns it directly and returns it;
	 * on a client the route goes to the server and this returns null, because the client does not
	 * own the road that will exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	ARealtimeRoadSplineActor* FinishRoadDraft();

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	void CancelRoadDraft();

	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	ARealtimeRoadJunctionActor* SpawnJunctionAtLocation(const FVector& WorldLocation, ERoadJunctionType JunctionType);

	/** Terrain height at a world XY, or the fallback Z when nothing is under the cursor. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	FVector ProjectOntoTerrain(const FVector& WorldLocation) const;

	/**
	 * Resolves a raw click into the location a road point should actually occupy: a junction leg
	 * socket when one is in range, otherwise the terrain surface under the click.
	 */
	FVector ResolvePlacementLocation(
		const FVector& RawLocation,
		ARealtimeRoadJunctionActor*& OutSnappedJunction,
		int32& OutSnappedLeg) const;

	/**
	 * Selects whatever road is at a location without drawing anything: an exact handle hit first,
	 * then a nearby control point, then the road's line — which gets an anchor point inserted so
	 * there is something to grab. Used by the Select tool.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool SelectRoadNear(const FVector& WorldLocation);

	/** Spawns a committed single-point road and selects it. The first click of a draw. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	ARealtimeRoadSplineActor* StartRoadAt(const FVector& WorldLocation, ERoadType RoadType);

	/** Creates an intersection where a road passes through a world location. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	ARealtimeRoadJunctionActor* CreateJunctionOnRoadAt(ARealtimeRoadSplineActor* Road, const FVector& WorldLocation);

	/** Nearest road whose line passes within RoadLineSnapRadius of a location, and where along it. */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool FindRoadLineNear(const FVector& WorldLocation, ARealtimeRoadSplineActor*& OutRoad, float& OutDistanceAlong) const;

protected:
	/** Rebuilds the overlay for the selected or hovered road and refreshes hover highlighting. */
	void UpdateEditOverlay();

	/** Handle under the cursor, if any. */
	bool TraceHandleUnderCursor(ARealtimeRoadSplineActor*& OutRoad, int32& OutPointIndex) const;

	/** Ground point under the cursor, ignoring the overlay and the road being dragged. */
	bool TraceGroundUnderCursor(FVector& OutLocation) const;

	APlayerController* GetOwningPlayerController() const;

	/** Keeps the shared gizmo targeted on the selected point's own spline component. */
	void UpdatePointGizmo();

	/** The cursor ray, for callers that need the raw origin/direction rather than a plane hit. */
	bool GetCursorRay(FVector& OutOrigin, FVector& OutDirection) const;

	/** Builds an FGizmoInteractionRay from the current cursor position, or returns false if the mouse can't be deprojected right now. */
	bool BuildGizmoInteractionRay(struct FGizmoInteractionRay& OutRay) const;

	/** The active camera's forward vector, for hit-testing handles whose plane depends on view direction (e.g. the screen-space rotation ring). */
	FVector GetCameraForwardVector() const;

	/**
	 * Reacts to an Escape-cancel landing on our own bound point (the subsystem is shared with the
	 * building tool, so a building-gizmo cancel must not touch road drag state) -- settles the road
	 * the same way EndPointDrag would, since the subsystem already restored the point's position
	 * and nothing else will tell this component the drag stopped.
	 */
	void HandleGizmoManipulationCancelled(USceneComponent* CancelledTarget);

	/** Finds the nearest selectable spline point across every non-preview road in the level. */
	bool FindRoadPointNear(
		const FVector& WorldLocation,
		ARealtimeRoadSplineActor*& OutRoad,
		int32& OutPointIndex) const;

	UFUNCTION(Server, Reliable)
	void Server_CommitRoad(const TArray<FVector>& WorldPoints, ERoadType RoadType);

	UFUNCTION(Server, Reliable)
	void Server_SpawnJunction(FVector WorldLocation, ERoadJunctionType JunctionType);

	/** Spawns a finished road on the authority. Shared by the local and RPC paths. */
	ARealtimeRoadSplineActor* SpawnRoadAuthoritative(const TArray<FVector>& WorldPoints, ERoadType RoadType);

	bool HasAuthority() const;

private:
	mutable TWeakObjectPtr<ATerraDyneManager> CachedTerraDyneManager;

	UPROPERTY(Transient)
	TArray<FRoadEditSnapshot> UndoStack;

	UPROPERTY(Transient)
	TArray<FRoadEditSnapshot> RedoStack;

	/** Captures a road's points, or returns false when there is nothing to capture. */
	bool CaptureSnapshot(ARealtimeRoadSplineActor* Road, FRoadEditSnapshot& OutSnapshot) const;

	/** Puts a snapshot back and rebuilds everything that depends on it. */
	bool ApplySnapshot(const FRoadEditSnapshot& Snapshot);

	/**
	 * Road the overlay geometry was last built for. Rebuilding the route line means refilling
	 * every instance, so it only happens when the target changes or its shape is actually moving,
	 * not on every tick. Highlight and selection tints are cheap and do refresh every frame.
	 */
	TWeakObjectPtr<ARealtimeRoadSplineActor> OverlayBuiltForRoad;

	/** Whether the current drag is a gizmo axis/plane/rotate/scale grab (subsystem-driven) rather than a free ground-follow drag -- queried once at BeginPointDrag, not re-derived every tick. */
	bool bIsGizmoDrag = false;
	FVector DragStartPointLocation = FVector::ZeroVector;

	/** Time since the last junction re-solve during the current drag. */
	float TimeSinceJunctionResolve = 0.0f;

	/**
	 * Offset from the cursor's ground point to the grabbed point, captured when the drag begins.
	 *
	 * Without it the point snaps to the cursor on the first frame of the drag. The handle floats
	 * above the road and the cursor ray meets terrain somewhere else entirely at a shallow camera
	 * angle, so that snap threw points a long way across the map.
	 */
	FVector DragGrabOffset = FVector::ZeroVector;
};
