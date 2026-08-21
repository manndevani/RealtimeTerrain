// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/TextureRenderTarget2D.h"
#include "World/TerraDyneTileData.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "TerraDyneChunk.generated.h"

/**
 * One sampled surface point for external scatter systems (e.g. a hand-authored PCG Graph).
 * Not tied to any specific mesh set — a PCG graph filters/spawns from these fields itself.
 */
USTRUCT(BlueprintType)
struct FTerraDyneScatterPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Scatter")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Scatter")
	FVector Normal = FVector::UpVector;

	/** 0 = flat, 1 = vertical, derived from Normal.Z. */
	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Scatter")
	float Slope01 = 0.f;
};

UCLASS()
class TERRADYNE_API ATerraDyneChunk : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneChunk();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "TerraDyne")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComp;

	UPROPERTY(EditAnywhere, Replicated, Category = "TerraDyne")
	float ZScale;

	UPROPERTY(EditAnywhere, Replicated, Category = "TerraDyne")
	float WorldSize;

	UPROPERTY(EditAnywhere, Replicated, Category = "TerraDyne")
	int32 Resolution;

	UPROPERTY(VisibleAnywhere, Replicated, Category = "TerraDyne")
	FIntPoint GridCoordinate;

	UPROPERTY(VisibleAnywhere, Replicated, Category = "TerraDyne")
	float ChunkSizeWorldUnits;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "TerraDyne|Procedural")
	int32 ProceduralSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "TerraDyne|Procedural")
	bool bIsAuthoredChunk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "TerraDyne|Procedural")
	FName PrimaryBiomeTag = NAME_None;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BrushMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HeightRT; // Current height RT (read target for display/collision)

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HeightRT_Swap; // Ping-pong write target for GPU brush

	// CPU-mode HeightMap source: HeightRT is only created/uploaded in the GPU brush path, so in
	// CPU mode it stays null and the material's HeightMap parameter would silently fall back to
	// the material's own default texture. This mirrors WeightTexture's pattern to give the
	// material real height data (HeightBuffer replicated into RGB, matching HeightRT's encoding)
	// whenever bUseGPU is false.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CPUHeightTexture;

	// --- Height Buffers ---
	TArray<float> BaseBuffer;
	TArray<float> SculptBuffer;
	TArray<float> DetailBuffer;
	TArray<float> HeightBuffer;

	// --- Building pad layer ---
	// Derived state, deliberately NOT serialized: ATerraDyneManager rebuilds it from the building
	// registry whenever a building moves, reshapes, or a chunk streams in. Nothing to migrate, no
	// way for a stale pad to outlive the building that made it.
	//
	// Stored as (weight, target) rather than a single height delta so that sculpting underneath a
	// building cannot make the pad stale. A delta is only correct against the terrain it was
	// computed from; a target is re-evaluated against whatever the terrain currently is, every
	// time RecomputeHeightFromLayers() runs.
	TArray<float> PadWeightBuffer;   // 0 = untouched ground, 1 = fully flattened to target
	TArray<float> PadTargetBuffer;   // normalized target height; only meaningful where weight > 0

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TransferredFoliageISMs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> TransferredActorFoliageActors;

public:
	void Initialize(int32 InResolution, float InSize);
	void InitializeChunk(FIntPoint Coord, float Size, int32 InRes, UTexture2D* SourceHeight);

	/** Dedicated Sandbox procedural population */
	void PopulateProcedural();

	/** Dedicated High-Performance DEM raster population (bypasses procedural noise & auto-material) */
	void PopulateFromDEM(TArray<float>&& InHeightBuffer, float InZScale, double InWorldZOffset, bool bGenerateCollision = false);

	void SetMaterial(UMaterialInterface* InMaterial);

	// BrushMode replaces bIsHole; FlattenHeight is mode-specific
	void ApplyLocalIdempotentEdit(
		FVector RelativePos,
		float Radius,
		float Strength,
		ETerraDyneBrushMode BrushMode,
		float FlattenHeight = 0.f);

	/**
	 * The one place HeightBuffer is composed from its layers:
	 *     Height = Clamp(Lerp(Base + Sculpt + Detail, PadTarget, PadWeight), 0, 1)
	 *
	 * Every path that rebuilds heights goes through here — sculpting, undo/redo, network sync,
	 * layer reset, load. Before this existed the same sum was written out by hand in seven places,
	 * which is exactly the kind of thing that leaves one of them behind when the formula changes.
	 */
	void RecomputeHeightFromLayers();

	/** Post-change housekeeping: mesh positions, height texture, collision + foliage debounce. */
	void NotifyHeightBufferChanged();

	//--- Building pad layer (written by ATerraDyneManager, never by the chunk itself) ---

	/** Drops the whole pad layer. The manager calls this before re-stamping overlapping buildings. */
	void ClearBuildingPad();

	/**
	 * Stamps one building footprint into the pad layer.
	 * @param LocalPolygonXY  Footprint in chunk-LOCAL XY.
	 * @param TargetLocalZ    Flatten height in chunk-local Z (world Z minus the chunk's actor Z).
	 * @param Falloff         Blend skirt width outside the footprint, in world units.
	 *
	 * Where two buildings overlap the stronger weight wins, so the pad you are most inside decides
	 * the height. That keeps the result independent of the order buildings are stamped in.
	 */
	void StampBuildingPad(const TArray<FVector2D>& LocalPolygonXY, float TargetLocalZ, float Falloff);

	/**
	 * True if any vertex currently carries pad weight.
	 *
	 * Tracked as a flag rather than scanned, because the manager asks this for every chunk on every
	 * stream-in — and in a world with no buildings at all that would otherwise be a full sweep of
	 * Resolution^2 floats per chunk load, to answer "no" every time.
	 */
	bool HasBuildingPad() const { return bHasBuildingPad; }

	void RebuildPhysicsMesh();
	float GetHeightAtLocation(FVector LocalPos) const;
	const TArray<float>& GetHeightBuffer() const { return HeightBuffer; }

	/** Terrain height ignoring any building pad — what the ground would be with no building on it. */
	float GetNaturalHeightAtLocation(FVector LocalPos) const;
	bool IsUsingGPU() const { return bUseGPU; }
	FBox GetWorldBounds() const;

	/**
	 * Samples a SamplesPerAxis x SamplesPerAxis grid across the chunk and returns world-space
	 * position/normal/slope data per point, for external scatter systems (e.g. a PCG Graph's
	 * "Execute Blueprint" / custom element) to filter and spawn meshes from.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Scatter")
	TArray<FTerraDyneScatterPoint> GetScatterPoints(int32 SamplesPerAxis = 32) const;

	// CPU-mode height texture upload (see CPUHeightTexture)
	void UploadCPUHeightTexture();

	// Layer weights are debounced like collision. Geometry must track the brush every frame, but the
	// grass/rock/snow split costs a blur plus a weight evaluation per vertex, and rebaking that on
	// every tick of a held stroke was the dominant cost of sculpting. A short delay is invisible —
	// the shape updates live and the colouring settles just behind it.
	void MarkLayerWeightsDirty();
	void RebakeLayerWeights();

	// Imported placed foliage
	void SetTransferredFoliageData(
		const TArray<FString>& InStaticMeshPaths,
		const TArray<int32>& InMaterialCounts,
		const TArray<FString>& InOverrideMaterialPaths,
		const TArray<int32>& InDefinitionIndices,
		const TArray<FTransform>& InLocalTransforms,
		const TArray<float>& InTerrainOffsets,
		bool bInFollowsTerrain);
	void RefreshTransferredFoliagePlacement(bool bSnapToTerrain);
	void RequestTransferredFoliageRefresh();
	void SetTransferredFoliageFollowsTerrain(bool bInFollowsTerrain);
	int32 GetTransferredFoliageInstanceCount() const { return TransferredFoliageInstanceLocalTransforms.Num(); }
	int32 GetTransferredFoliageDefinitionCount() const { return TransferredFoliageStaticMeshPaths.Num(); }
	void SetTransferredActorFoliageData(
		const TArray<FString>& InActorClassPaths,
		const TArray<uint8>& InAttachFlags,
		const TArray<int32>& InDefinitionIndices,
		const TArray<FTransform>& InLocalTransforms,
		const TArray<float>& InTerrainOffsets);
	void RefreshTransferredActorFoliagePlacement(bool bSnapToTerrain);
	int32 GetTransferredActorFoliageInstanceCount() const { return TransferredActorFoliageInstanceLocalTransforms.Num(); }
	int32 GetTransferredActorFoliageDefinitionCount() const { return TransferredActorFoliageClassPaths.Num(); }
	bool GetTransferredActorFoliageInstanceTransform(int32 InstanceIndex, FTransform& OutTransform) const;

	// Persistence
	struct FTerraDyneChunkData GetSerializedData();
	void LoadFromData(const struct FTerraDyneChunkData& Data);

	// LOD & Optimization
	void UpdateLOD(const FVector& ViewerPos);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	bool bInitialized;
	bool bUseGPU;
	/** Mirrors "PadWeightBuffer has a non-zero entry" — see HasBuildingPad. */
	bool bHasBuildingPad = false;
	void UpdateDisplayMaterialParameters();
	
	// Collision Throttling
	float CollisionDebounceTimer;
	bool bCollisionDirty;

	float LayerWeightsDebounceTimer = 0.0f;
	bool bLayerWeightsDirty = false;

	// Imported foliage refresh debounce
	float TransferredFoliageDebounceTimer;
	bool bTransferredFoliageDirty;
	bool bTransferredFoliageFollowsTerrain;
	TArray<FString> TransferredFoliageStaticMeshPaths;
	TArray<int32> TransferredFoliageMaterialCounts;
	TArray<FString> TransferredFoliageOverrideMaterialPaths;
	TArray<int32> TransferredFoliageDefinitionIndices;
	TArray<FTransform> TransferredFoliageInstanceLocalTransforms;
	TArray<float> TransferredFoliageInstanceTerrainOffsets;
	TArray<FString> TransferredActorFoliageClassPaths;
	TArray<uint8> TransferredActorFoliageAttachFlags;
	TArray<int32> TransferredActorFoliageDefinitionIndices;
	TArray<FTransform> TransferredActorFoliageInstanceLocalTransforms;
	TArray<float> TransferredActorFoliageInstanceTerrainOffsets;

	bool SetupGPU();
	void UpdateRenderTargetFromHeightmap();
	void ReadbackRenderTarget();
	void ApplyBrushGPU(FVector LocalPos, float Radius, float Strength);
	void RebuildMesh();
	// Cheaper path for the interactive-sculpting hot path: updates existing vertex Z positions
	// and normals in place instead of clearing and re-triangulating the whole mesh (topology
	// never changes during sculpting, only height). Falls back to RebuildMesh() automatically
	// if the mesh's vertex count doesn't already match (not yet built, or Resolution changed).
	void UpdateMeshHeights();
	void UpdateCollision();
	void ClearTransferredFoliageComponents();
	void ClearTransferredActorFoliageActors();
};
