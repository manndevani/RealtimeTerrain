// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building/GRTBuildingTypes.h"
#include "GRTBuildingActor.generated.h"

class USplineComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UGRTBuildingKit;

/**
 * A modular building generated from a closed footprint spline.
 *
 * The spline is treated as a POLYLINE — every point is forced to linear tangents. The kit is made
 * of flat wall panels, so a curved segment would either need sub-module pieces or leave visible
 * gaps between panels. If curved buildings are ever wanted, that is a generator change, not a
 * setting.
 *
 * Geometry goes into pooled UInstancedStaticMeshComponents, one per unique mesh, so a ten-segment
 * five-floor building costs a handful of draw calls rather than a few hundred.
 */
UCLASS(Blueprintable)
class GRT_API AGRTBuildingActor : public AActor
{
	GENERATED_BODY()

public:
	AGRTBuildingActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT|Building")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT|Building")
	TObjectPtr<USplineComponent> FootprintSpline;

	/** Storeys. Each is exactly Kit->FloorHeight tall — no scaling, so panels never stretch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FloorCount, Category = "GRT|Building",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "20"))
	int32 FloorCount = 2;

	/** Leave null to use the built-in SD_Art Industrial kit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Kit, Category = "GRT|Building")
	TObjectPtr<UGRTBuildingKit> Kit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_BuildingConfig, Category = "GRT|Building|Parts")
	bool bGenerateWalls = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_BuildingConfig, Category = "GRT|Building|Parts")
	bool bGenerateFloors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_BuildingConfig, Category = "GRT|Building|Parts")
	bool bGenerateRoof = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_BuildingConfig, Category = "GRT|Building|Parts")
	bool bGenerateCornerPosts = true;

	/** Which footprint segment gets the door panel on the ground floor. Negative disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_BuildingConfig, Category = "GRT|Building|Parts")
	int32 DoorSegmentIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "GRT|Building")
	FGuid BuildingId;

	/** Replicated local spline points to keep footprints in sync across clients. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_SplinePoints, Category = "GRT|Building")
	TArray<FVector> ReplicatedSplinePoints;

	/**
	 * Explicit scale replication -- SetReplicateMovement's FRepMovement (Location/Rotation only) has
	 * no Scale3D field at all, so a gizmo Scale-mode drag would otherwise resize the building only on
	 * whichever machine actually performed the drag, and every other client would keep watching it at
	 * its old size forever. NotifyBuildingChanged() keeps this in sync with the real GetActorScale3D()
	 * on the authority; OnRep_Scale3D applies it back out on every other machine.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Scale3D, Category = "GRT|Building")
	FVector ReplicatedScale3D = FVector::OneVector;

	//--- Terrain pad ---

	/** When false the building leaves the ground alone and simply sits wherever it is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PadSettings, Category = "GRT|Building|Terrain")
	bool bFlattenTerrain = true;

	/** Level the pad to the average natural ground under the footprint, rather than PadTargetWorldZ. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PadSettings, Category = "GRT|Building|Terrain")
	bool bAutoPadHeight = true;

	/** Explicit flatten height in world Z. Only used when bAutoPadHeight is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PadSettings, Category = "GRT|Building|Terrain",
		meta = (EditCondition = "!bAutoPadHeight"))
	float PadTargetWorldZ = 0.0f;

	/** Width of the blend skirt from the footprint edge out to natural ground, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PadSettings, Category = "GRT|Building|Terrain",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2000.0"))
	float PadFalloff = 400.0f;

	/** When true, this is a local preview ghost that has no collision, does not stamp pads, and displays as a hologram. */
	UPROPERTY(Transient)
	bool bIsHolographicPreview = false;

	UFUNCTION(BlueprintCallable, Category = "GRT|Building")
	void SetIsHolographicPreview(bool bHolographic);

	void ApplyHolographicVisuals();

	/**
	 * The height the pad will actually flatten to, resolving bAutoPadHeight against the terrain.
	 *
	 * The auto height is sampled once and then cached until the building actually changes. It has
	 * to be: the sample can only read chunks that are currently loaded, so re-deriving it on every
	 * refresh would give a different answer as the terrain streams in, and a building spanning two
	 * chunks would end up with a step in its pad where the two answers met.
	 */
	UFUNCTION(BlueprintCallable, Category = "GRT|Building|Terrain")
	float ResolvePadTargetWorldZ() const;

	/** Forces the auto pad height to be re-sampled from the terrain on the next refresh. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GRT|Building|Terrain")
	void RelevelToTerrain();

	/** Drops the building so its ground floor sits on the pad height. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GRT|Building|Terrain")
	void SnapToPadHeight();

	/**
	 * Re-stamps the terrain pad for wherever this building is now, and for wherever it used to be.
	 * Call after any change to position, rotation, scale, footprint, or pad settings. On the
	 * authority, also resyncs ReplicatedScale3D from the actor's real current scale -- the one
	 * transform channel Unreal's own movement replication doesn't cover.
	 */
	UFUNCTION(BlueprintCallable, Category = "GRT|Building|Terrain")
	void NotifyBuildingChanged();

	//--- Generation ---

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GRT|Building")
	void RegenerateBuilding();

	/** Replaces the footprint with a centred rectangle snapped to the kit's module grid. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GRT|Building")
	void ResetToDefaultFootprint();

	UFUNCTION(BlueprintCallable, Category = "GRT|Building")
	void SetFloorCount(int32 NewCount);

	//--- Queries (phase 2 onwards) ---

	/** Total height in world units. */
	UFUNCTION(BlueprintPure, Category = "GRT|Building")
	float GetBuildingHeight() const;

	/** Footprint as a closed polygon in actor-local XY. */
	TArray<FVector2D> GetFootprintPolygonLocal() const;

	/** Footprint as a closed polygon in world XY — what the terrain pad is computed from. */
	TArray<FVector2D> GetFootprintPolygonWorld() const;

	/** World-space XY bounds of the footprint, expanded by Padding. */
	FBox2D GetFootprintBoundsWorld(float Padding = 0.0f) const;

	void SyncSplinePointsToReplication();

	/** Local-space footprint points, in order. */
	TArray<FVector> GatherSplinePointsLocal() const;

	/** Updates one local spline point position and rebuilds geometry. */
	void SetSplinePointLocal(int32 PointIndex, const FVector& LocalPos, bool bUpdatePad = true);

	/** Sets local footprint spline points and regenerates the building and pad. */
	void SetSplinePoints(const TArray<FVector>& InPoints);

	/** Adds a new corner/spline point to the building footprint. */
	void AddSplinePoint();

	/** Removes a spline point by index, maintaining at least 3 points. */
	void RemoveSplinePoint(int32 PointIndex = INDEX_NONE);

	void EnforceLinearTangents();

	//--- Serialization (phase 6) ---
	FGRTBuildingData ToData() const;
	void ApplyData(const FGRTBuildingData& Data, bool bRegenerate = true);

	/**
	 * Spawns a building and restores Data onto it. The pad settings are written before
	 * FinishSpawning, so a building saved with bFlattenTerrain off never stamps a pad in the
	 * construction/BeginPlay pass that runs inside the spawn. Used by save-game load and undo.
	 */
	static AGRTBuildingActor* SpawnFromData(UWorld* World, const FGRTBuildingData& Data);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostNetReceiveLocationAndRotation() override;

	UFUNCTION()
	void OnRep_FloorCount();

	UFUNCTION()
	void OnRep_Kit();

	UFUNCTION()
	void OnRep_BuildingConfig();

	UFUNCTION()
	void OnRep_PadSettings();

	UFUNCTION()
	void OnRep_SplinePoints();

	UFUNCTION()
	void OnRep_Scale3D();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	/** One planned wall panel: which width, and how far along the segment it starts. */
	struct FPanelPlacement
	{
		int32 Width = 0;
		float Offset = 0.0f;
	};

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Instancers;

	/** Guards against regenerating while the actor is still loading its properties. */
	bool bSuppressRegenerate = false;

	/**
	 * Footprint bounds as of the last pad refresh. A move has to recompute the ground it left as
	 * well as the ground it arrived on, or the old pad stays stamped into those chunks.
	 */
	FBox2D LastPadBoundsWorld = FBox2D(ForceInit);

	/** Sampled-once auto pad height. See ResolvePadTargetWorldZ for why this is cached. */
	mutable float CachedPadTargetWorldZ = 0.0f;
	mutable bool bPadTargetCached = false;

	class AGRTManager* FindManager() const;

	/**
	 * Vertical line trace fallback for ResolvePadTargetWorldZ's footprint samples, for whatever
	 * ground has no GRT chunk under it -- a locally-imported Cesium tile terrain, most notably
	 * (see AGRTManager::ImportCesiumTerrainTileset), which renders real collidable geometry on
	 * ECC_WorldStatic but is invisible to Manager->GetNaturalTerrainHeightAtXY's chunk-buffer lookup.
	 * Mirrors RoadMesh::SampleGroundHeight's same fallback for the same reason.
	 */
	bool TraceGroundHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const;

	/** Falls back to the class defaults (the SD_Art Industrial set) when no kit is assigned. */
	const UGRTBuildingKit* ResolveKit() const;
	UInstancedStaticMeshComponent* GetOrCreateInstancer(UStaticMesh* Mesh);
	void ClearAllInstances();

	/** Greedy largest-first fill of one segment. Handles the sub-module remainder by overlap. */
	TArray<FPanelPlacement> PlanSegmentPanels(float SegmentLength, const UGRTBuildingKit* InKit) const;

	void BuildWalls(const TArray<FVector>& Points, const UGRTBuildingKit* InKit);
	void BuildCornerPosts(const TArray<FVector>& Points, const UGRTBuildingKit* InKit);
	void BuildSlabs(const TArray<FVector>& Points, const UGRTBuildingKit* InKit);

	/**
	 * Places one slab cell, subdividing into smaller tiles wherever a full one would overhang the
	 * footprint. CellCorner is the tile pivot: the cell spans +X and -Y from it, matching the mesh.
	 */
	void PlaceSlabCell(
		const TArray<FVector2D>& Polygon,
		const FVector2D& CellCorner,
		int32 TileIndex,
		const TArray<struct FGRTFloorTileEntry>& Tiles,
		float LevelZ);

	/** True when the whole cell lies inside the polygon, not merely its centre. */
	static bool IsCellInsidePolygon(const TArray<FVector2D>& Polygon, const FVector2D& CellCorner, float Size);

	static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);
};
