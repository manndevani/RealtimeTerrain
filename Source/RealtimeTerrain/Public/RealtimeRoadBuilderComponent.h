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
class APlayerController;

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

	/**
	 * Starts dragging a control point. Call on mouse down: if a handle is under the cursor it
	 * becomes the selection first, so grabbing a point and moving it is one gesture.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Builder")
	bool BeginPointDrag();

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

	/**
	 * Road the overlay geometry was last built for. Rebuilding the route line means refilling
	 * every instance, so it only happens when the target changes or its shape is actually moving,
	 * not on every tick. Highlight and selection tints are cheap and do refresh every frame.
	 */
	TWeakObjectPtr<ARealtimeRoadSplineActor> OverlayBuiltForRoad;
};
