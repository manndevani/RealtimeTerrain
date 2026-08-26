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
class URoadStylePreset;

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
	virtual void Destroyed() override;
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

	/**
	 * Optional named lane style (see RoadStylePresets.h), swept via
	 * RoadMesh::BuildRoadSectionFromStyle instead of the CrossSectionSettings-driven section
	 * above. Unset (the default) leaves every existing road's behavior unchanged.
	 *
	 * CrossSectionSettings.RoadWidth still independently drives terrain-pad width, junction half-
	 * width and crossing-merge radius even when a style is set -- set it to roughly the style's
	 * own total width for those to line up with what the style actually renders. Auto-deriving it
	 * from the style is a follow-on, not yet wired.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_RoadConfig, Category = "Road|Style")
	TObjectPtr<URoadStylePreset> StylePreset;

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
	 * across the width over TerraDyne's own terrain (a GDAL-imported DEM or Sandbox procedural
	 * ground), which is what makes them read as engineered rather than draped -- this toggle and
	 * MaxCrossSlopeDegrees don't touch that. See MaxPavedCrossSlopeDegrees for the one deliberate
	 * exception: a paved road does get a small amount of tilt where it crosses a locally-imported
	 * Cesium tile terrain specifically, since real Cesium mountain relief can be steep enough that a
	 * perfectly rigid road section looks obviously wrong against it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (EditCondition = "RoadType == ERoadType::RoughRoad"))
	bool bRoughRoadFollowsCrossSlope = true;

	/** Ceiling on terrain-driven cross slope, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (ClampMin = "0.0", ClampMax = "60.0", EditCondition = "bRoughRoadFollowsCrossSlope"))
	float MaxCrossSlopeDegrees = 22.0f;

	/**
	 * Ceiling on cross slope for a PAVED road, applied only where it crosses ground with no
	 * TerraDyne chunk under it (in practice, a locally-imported Cesium tile terrain -- see
	 * ATerraDyneManager::ImportCesiumTerrainTileset). Always on, unlike rough roads' toggle: this
	 * is a narrow, automatically-scoped exception to "paved roads stay level," not a general paved-
	 * road banking feature, so there is nothing for the exception itself to need turning off. Left
	 * noticeably smaller than MaxCrossSlopeDegrees by default -- an engineered road banks far less
	 * aggressively than a dirt trail even where it does bank at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain", meta = (ClampMin = "0.0", ClampMax = "30.0", EditCondition = "RoadType == ERoadType::PavedRoad"))
	float MaxPavedCrossSlopeDegrees = 8.0f;

	//--- TERRAIN REBASE ---

	/**
	 * Level the ground under the road to sit just beneath it.
	 *
	 * Uses TerraDyne's pad layer, the same non-destructive channel building foundations use, rather
	 * than the flatten brush: the sculpt buffer is never written, so removing the road restores the
	 * original landform exactly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain Rebase")
	bool bRebaseTerrain = true;

	/** Extra ground levelled either side of the road's own width, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain Rebase", meta = (ClampMin = "0.0"))
	float PadWidthMargin = 150.0f;

	/** Width of the blend from the levelled strip back to natural ground, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain Rebase", meta = (ClampMin = "1.0"))
	float PadFalloff = 350.0f;

	/**
	 * Spacing in cm between pad quads. Each quad is level, so this trades how closely the rebased
	 * ground tracks a sloping road against how many stamps the terrain has to rasterise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain Rebase", meta = (ClampMin = "100.0"))
	float PadSampleSpacing = 200.0f;

	/**
	 * How far below the road surface the ground is levelled, on top of the slab thickness.
	 *
	 * This is the clearance that absorbs the difference between a level pad quad and the sloping
	 * road over it, plus the terrain heightfield's own resolution between grid vertices. Too small
	 * and the ground surfaces through the road; the previous default of 10 cm left about 2 cm of
	 * real headroom and broke through constantly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Terrain Rebase", meta = (ClampMin = "0.0"))
	float PadDepthBelowSurface = 60.0f;

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

	/**
	 * Extends the road from a point to a target, inserting intermediate points along the way. An
	 * interior FromPointIndex can't simply be extended -- the road already continues past it -- so
	 * that case branches into a new road instead (see BranchRoadFromPoint).
	 *
	 * Returns the actor whose point should now be treated as selected: this actor for an in-place
	 * append/prepend, or the newly spawned branch road when extending from an interior point.
	 * OutSelectedPointIndex is that actor's newly-active point index. Callers should re-anchor
	 * their own selection (SelectedRoadActor/SelectedPointIndex) from these two outputs rather than
	 * re-deriving "which end grew" themselves -- this function is the only place that actually knows.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	ARealtimeRoadSplineActor* ExtendFromPointToLocation(int32 FromPointIndex, const FVector& TargetWorldLocation, int32& OutSelectedPointIndex, float MaxSegmentSpacing = 800.0f);

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
	 * Re-solves every junction this road reaches, so moving the road reshapes its intersections
	 * instead of leaving them behind.
	 *
	 * Covers junctions that already reference this road and any whose bounds the route now passes
	 * through, which is what lets a road dragged into a junction be picked up by it.
	 *
	 * bFullNetworkReconcile controls how the rest of the level's crossings get reconciled afterward.
	 * True (the default, used by every commit-time caller) runs the full, authoritative
	 * RoadNetwork::RebuildIntersections over every road in the world -- correct, but too costly to
	 * repeat many times a second. False (used only by the interactive per-tick drag path) scopes
	 * that pass to crossings involving this road via RebuildIntersectionsNear: the junction actually
	 * being dragged toward/through still reshapes live with no visible difference, but crossings
	 * between two other, untouched roads are skipped rather than needlessly re-solved every tick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void RefreshAffectedJunctions(bool bFullNetworkReconcile = true);

	/**
	 * Refreshes the render mesh now and schedules the collision-bearing rebuild for once edits
	 * stop. This is the call to use from a drag loop.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	void RequestRebuild();

	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	bool SampleTerrainHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const;

	/**
	 * Driving lanes this road carries, for junction ramp/fork classification
	 * (RoadNetworkSolver::IsRampConnection). StylePreset's own count when one is set; otherwise a
	 * fallback matching what CrossSectionSettings has always implied -- two lanes (one each
	 * direction) for a paved road, one for a rough trail.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road|Realtime")
	int32 GetDrivingLaneCount() const;

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

	/** Pushes this road's pad footprint to the terrain manager and refreshes affected chunks. */
	void UpdateTerrainPad(const TArray<TArray<RoadMesh::FFrame>>& RunFrames);

	/** Drops this road's pad, restoring the natural landform underneath. */
	void ClearTerrainPad();
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

	/** Per-station roll for rough roads (and paved roads over Cesium tile terrain) following the terrain's lateral slope. */
	void ComputeCrossSlopeRoll(const TArray<RoadMesh::FFrame>& Frames, TArray<double>& OutRoll) const;

	/**
	 * As SampleTerrainHeightAtXY, but also reports whether the sample came from TerraDyne's own
	 * heightfield or the raycast fallback (i.e. a locally-imported Cesium tile terrain, or anything
	 * else with no TerraDyne chunk under it). Kept separate from the public, Blueprint-callable
	 * SampleTerrainHeightAtXY rather than adding a parameter there -- a raw bool* out-param is not a
	 * pattern UHT reflects cleanly for Blueprint.
	 */
	bool SampleTerrainHeightAtXYWithSource(const FVector2D& WorldXY, float& OutHeightZ, bool& OutWasNonTerraDyneGround) const;

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
