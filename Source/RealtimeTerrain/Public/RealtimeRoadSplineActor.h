// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "RoadTypes.h"
#include "RealtimeRoadSplineActor.generated.h"

class USplineComponent;
class UDynamicMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class ARealtimeRoadJunctionActor;
class ATerraDyneManager;

namespace RoadMesh { struct FFrame; }

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoadMeshUpdated, ARealtimeRoadSplineActor*, RoadActor);

/**
 * A road built by sweeping a solid cross-section along a spline.
 *
 * The spline supplies the route in XY only. Height comes from sampling TerraDyne terrain at every
 * swept station and smoothing the result, so the road follows the landform at mesh resolution
 * rather than only at the spline's control points.
 */
UCLASS(Blueprintable, ClassGroup=(Roads))
class REALTIMETERRAIN_API ARealtimeRoadSplineActor : public AActor
{
	GENERATED_BODY()

public:
	ARealtimeRoadSplineActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Route of the road. Only the XY of this spline is used when terrain conforming is on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road")
	TObjectPtr<USplineComponent> SplineComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road")
	TObjectPtr<UDynamicMeshComponent> RoadMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ScatterHISMComponent;

	//--- CONFIGURATION ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_RoadConfig, Category = "Road|Configuration")
	ERoadType RoadType = ERoadType::PavedRoad;

	/**
	 * Slab shape and tessellation. Applies to both road types; rough roads ignore the curb and
	 * footpath fields rather than hiding the whole struct, which used to make a rough road's width
	 * uneditable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_RoadConfig, Category = "Road|Cross Section")
	FRoadCrossSectionSettings CrossSectionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_RoadConfig, Category = "Road|Rough Settings", meta = (EditCondition = "RoadType == ERoadType::RoughRoad"))
	FRoadScatterSettings ScatterSettings;

	//--- TERRAIN CONFORMING ---

	/** Take the road's height from the terrain at every swept station rather than from the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain")
	bool bConformToTerrainInRealtime = true;

	/** Lift above the sampled ground, in cm. Absorbs terrain tessellation error between vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (ClampMin = "0.0"))
	float TerrainSurfaceOffset = 8.0f;

	/**
	 * Half-width, in stations, of the moving average applied to sampled ground height. Raising it
	 * makes the road ride over small bumps instead of tracing them; zero disables smoothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (ClampMin = "0", ClampMax = "20"))
	int32 TerrainSmoothingSamples = 3;

	/**
	 * Let a rough road roll with the terrain's lateral slope. Paved roads keep their section level
	 * across the width, which is what makes them read as engineered rather than draped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (EditCondition = "RoadType == ERoadType::RoughRoad"))
	bool bRoughRoadFollowsCrossSlope = true;

	/** Ceiling on terrain-driven cross slope, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (ClampMin = "0.0", ClampMax = "60.0", EditCondition = "bRoughRoadFollowsCrossSlope"))
	float MaxCrossSlopeDegrees = 22.0f;

	/** Whether this is a local placement preview ghost. Previews never replicate and never collide. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Road|Realtime")
	bool bIsHolographicPreview = false;

	/**
	 * Seconds of quiet after an interactive edit before the collision-bearing rebuild runs.
	 * Edits during the delay only refresh the render mesh, which is what keeps dragging smooth.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Realtime", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SettleDelaySeconds = 0.2f;

	//--- REPLICATED SPLINE POINTS ---

	UPROPERTY(ReplicatedUsing = OnRep_SplinePoints)
	TArray<FVector> ReplicatedWorldPoints;

	UFUNCTION()
	void OnRep_SplinePoints();

	UFUNCTION()
	void OnRep_RoadConfig();

	//--- MATERIALS ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Materials")
	TObjectPtr<UMaterialInterface> AsphaltMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Materials", meta = (EditCondition = "RoadType == ERoadType::PavedRoad"))
	TObjectPtr<UMaterialInterface> CurbMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Materials", meta = (EditCondition = "RoadType == ERoadType::PavedRoad"))
	TObjectPtr<UMaterialInterface> FootpathMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Materials", meta = (EditCondition = "RoadType == ERoadType::RoughRoad"))
	TObjectPtr<UMaterialInterface> RoughRoadMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Materials")
	TObjectPtr<UMaterialInterface> HolographicMaterial;

	//--- JUNCTION ATTACHMENT ---

	/** Junction the first spline point connects to. The road's start is pinned to its leg socket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction Alignment")
	TObjectPtr<ARealtimeRoadJunctionActor> StartJunction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction Alignment")
	int32 StartJunctionLegIndex = 0;

	/** Junction the last spline point connects to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction Alignment")
	TObjectPtr<ARealtimeRoadJunctionActor> EndJunction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction Alignment")
	int32 EndJunctionLegIndex = 0;

	/**
	 * Stretches of this road occupied by junctions. The mesh skips the stations inside each cut
	 * and pins the two exposed ends to that junction's leg sockets, so one road can pass through
	 * any number of intersections without ever being split into separate actors.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_JunctionCuts, Category = "Road|Junction Alignment")
	TArray<FRoadJunctionCut> JunctionCuts;

	UFUNCTION()
	void OnRep_JunctionCuts();

	/** Adds a cut and rebuilds. Overlapping cuts for the same junction are replaced, not stacked. */
	UFUNCTION(BlueprintCallable, Category = "Road|Junction Alignment")
	void AddJunctionCut(const FRoadJunctionCut& Cut);

	/** Drops every cut belonging to a junction, restoring the road through it. */
	UFUNCTION(BlueprintCallable, Category = "Road|Junction Alignment")
	void RemoveJunctionCutsFor(ARealtimeRoadJunctionActor* Junction);

	/** How many stations the endpoint pin is blended over, so a snapped end does not kink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction Alignment", meta = (ClampMin = "1", ClampMax = "20"))
	int32 JunctionBlendStations = 4;

	UPROPERTY(BlueprintAssignable, Category = "Road|Events")
	FOnRoadMeshUpdated OnRoadMeshUpdated;

	//--- REALTIME APIS ---

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void SetIsHolographicPreview(bool bHolographic);

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void AddPointRealtime(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void SetPointsRealtime(const TArray<FVector>& WorldPoints);

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void MovePointRealtime(int32 PointIndex, const FVector& NewWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void ClearPointsRealtime();

	/** Extends the road from a point to a target, inserting intermediate points along the way. */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void ExtendFromPointToLocation(int32 FromPointIndex, const FVector& TargetWorldLocation, float MaxSegmentSpacing = 800.0f);

	/** Spawns a junction at an interior point and a new road leading away from it. */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	ARealtimeRoadSplineActor* BranchRoadFromPoint(int32 PointIndex, const FVector& BranchTargetLocation, float MaxSegmentSpacing = 800.0f);

	/**
	 * Inserts a control point at a distance along the spline and returns its index, so a click
	 * anywhere on a road can become a selectable anchor rather than only its existing points.
	 * Reuses a nearby existing point instead of stacking a duplicate on top of it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	int32 InsertPointAtDistance(float Distance);

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	int32 FindClosestSplinePoint(const FVector& WorldLocation, float& OutDistance) const;

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	FVector GetSplinePointWorldLocation(int32 PointIndex) const;

	/** Full rebuild including collision. Cancels any pending settle pass. */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void RebuildRoad();

	/**
	 * Refreshes the render mesh now and schedules the collision-bearing rebuild for once edits
	 * stop. This is the call to use from a drag loop.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void RequestRebuild();

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	bool SampleTerrainHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const;

	/** True when this instance may edit its own points: the authority, or a local-only preview. */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	bool CanEditPoints() const;

protected:
	/** One continuous stretch of road between junction cuts, with whatever it is pinned to. */
	struct FRoadRun
	{
		TArray<FVector> Points;
		ARealtimeRoadJunctionActor* StartPinJunction = nullptr;
		int32 StartPinLeg = INDEX_NONE;
		ARealtimeRoadJunctionActor* EndPinJunction = nullptr;
		int32 EndPinLeg = INDEX_NONE;
	};

	void RebuildInternal(bool bUpdateCollision);
	void GenerateRoadMesh(bool bUpdateCollision);
	void GenerateScatter(const TArray<TArray<RoadMesh::FFrame>>& RunFrames);
	void ApplyMaterials();
	void SyncSplineFromReplicatedPoints();
	void CaptureReplicatedPoints();

	/**
	 * Stations along the route with terrain-conformed, smoothed heights, plus each station's
	 * distance along the spline so junction cuts can be located against it.
	 */
	bool BuildConformedCentreline(TArray<FVector>& OutPoints, TArray<double>& OutDistances) const;

	/** Splits the stations into the runs between junction cuts and pins each run's exposed ends. */
	void BuildRuns(const TArray<FVector>& Points, const TArray<double>& Distances, TArray<FRoadRun>& OutRuns) const;

	/** Per-station roll for rough roads following the terrain's lateral slope. */
	void ComputeCrossSlopeRoll(const TArray<RoadMesh::FFrame>& Frames, TArray<double>& OutRoll) const;

	/** Pins a run's ends to their leg sockets, blended over several stations so they do not kink. */
	void PinRunEnds(FRoadRun& Run) const;

private:
	/**
	 * Cached so terrain sampling does not iterate every actor in the level per sample. Weak, so it
	 * needs no UPROPERTY to stay safe across a level transition.
	 */
	mutable TWeakObjectPtr<ATerraDyneManager> CachedTerraDyneManager;

	FTimerHandle SettleTimerHandle;
};
