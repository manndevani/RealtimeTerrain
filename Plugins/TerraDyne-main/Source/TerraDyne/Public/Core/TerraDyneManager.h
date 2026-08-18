// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TerraDyneWorldTypes.h"
#include "World/TerraDyneTileData.h"
#include "Core/TerraDyneSaveGame.h"
#include "TerraDyneManager.generated.h"

// Forward Declarations
class ATerraDyneChunk;
class ALandscapeProxy;
class ULandscapeComponent;
class UMaterialInterface;
class ULandscapeLayerInfoObject;
class USceneComponent;
class UTerraDyneWorldPreset;
class ATerraDyneEditController;

/** Buffers snapshot for a single chunk before a stroke begins. */
struct FTerraDyneChunkSnapshot
{
	FIntPoint Coordinate;
	TArray<float> SculptBuffer;
};

/** One undo/redo history entry — covers all chunks touched by a single stroke. */
struct FTerraDyneUndoEntry
{
	TMap<FIntPoint, FTerraDyneChunkSnapshot> Snapshots;
};

USTRUCT(BlueprintType)
struct FTerraDyneGPUStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	FString AdapterName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	bool bIsCudaAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	int32 DedicatedVideoMemory = 0; // in MB

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	FString ComputeBackend; // GPU+CPU Hybrid or CPU Fallback
};

USTRUCT(BlueprintType)
struct FTerraDyneLandscapeMigrationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bHideSourceLandscape = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bClearExistingChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bPauseStreamingDuringImport = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bImportWeightLayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bCaptureLayerMappings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bAdoptLandscapeMaterialAsMasterMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bTransferPlacedFoliage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bTransferredFoliageFollowsTerrain = true;
};

UCLASS(Blueprintable)
class TERRADYNE_API ATerraDyneManager : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneManager();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TObjectPtr<USceneComponent> SceneRoot;

	//--- CONFIGURATION ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	TObjectPtr<ALandscapeProxy> TargetLandscapeSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Import Settings")
	float GlobalChunkSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Config")
	TSubclassOf<ATerraDyneChunk> ChunkClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Config")
	ETerraDyneLayer ActiveLayer = ETerraDyneLayer::Sculpt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterMaterial, Category = "TerraDyne|Config")
	TObjectPtr<UMaterialInterface> MasterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Tools")
	TObjectPtr<UMaterialInterface> HeightBrushMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	FTerraDyneLandscapeMigrationOptions LandscapeMigrationOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_LandscapeMigrationState, Category = "TerraDyne|Migration")
	FTerraDyneLandscapeMigrationState LandscapeMigrationState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Framework")
	TObjectPtr<UTerraDyneWorldPreset> WorldPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Procedural")
	FTerraDyneProceduralWorldSettings ProceduralWorldSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Procedural")
	TArray<FTerraDyneBiomeOverlay> BiomeOverlays;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Population")
	TArray<FTerraDynePopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneAISpawnZone> AISpawnZones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneBuildPermissionZone> BuildPermissionZones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	bool bRefreshNavMeshOnTerrainChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	bool bRefreshNavMeshOnPopulationChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	float NavigationDirtyBoundsPadding = 300.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Population")
	TArray<FTerraDynePersistentPopulationEntry> PersistentPopulationEntries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Procedural")
	TArray<FTerraDyneProceduralChunkState> ProceduralChunkStates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bAutoImportAtRuntime = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSpawnDefaultChunksOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSetupDefaultLightingOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSpawnShowcaseOnBeginPlay = false;

	//--- RUNTIME API ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Interaction")
	void ApplyGlobalBrush(
		FVector WorldLocation,
		float Radius,
		float Strength,
		ETerraDyneBrushMode BrushMode,
		float FlattenHeight = 0.f);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Interaction")
	void ApplyGlobalNoise(float Strength, float Frequency, float Seed);

	/**
	 * Swap the terrain material for the whole world — every loaded chunk plus any that stream in
	 * afterwards. Intended for biome switching: author one material instance per biome (desert,
	 * tundra, etc.) and swap between them.
	 *
	 * Server only. Clients pick the change up through OnRep_MasterMaterial, which is why the
	 * authority has to apply it explicitly here — a ReplicatedUsing notify does not fire on the
	 * machine that set the value.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Config")
	void SetMasterMaterial(UMaterialInterface* NewMaterial);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Query")
	ATerraDyneChunk* GetChunkAtLocation(FVector WorldLocation) const;

	/** Returns all active chunks whose AABB overlaps the given world-space circle. */
	TArray<ATerraDyneChunk*> GetChunksInRadius(FVector WorldLocation, float Radius);

	/** Returns all active chunks whose footprint overlaps the given world-space XY box. */
	TArray<ATerraDyneChunk*> GetChunksInBoxXY(const FBox2D& WorldAreaXY) const;

	/**
	 * Terrain height at a world XY, ignoring any building pad.
	 * Returns false when no loaded chunk covers that spot.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Query")
	bool GetNaturalTerrainHeightAtXY(FVector2D WorldXY, float& OutWorldZ) const;

	//--- Buildings ---

	/**
	 * Buildings register themselves so the manager can rebuild terrain pads without having to
	 * sweep the level for actors. Weak pointers: a building destroyed by any route just drops out.
	 */
	void RegisterBuilding(class ATerraDyneBuildingActor* Building);
	void UnregisterBuilding(class ATerraDyneBuildingActor* Building);

	/**
	 * Rebuilds the pad layer for every loaded chunk overlapping WorldAreaXY.
	 *
	 * Callers pass the union of a building's OLD and NEW footprint bounds, so the chunks it has
	 * just left are recomputed too — otherwise a moved building leaves its pad behind.
	 */
	void RefreshBuildingPads(const FBox2D& WorldAreaXY);

	/** Rebuilds one chunk's pad layer from scratch. Cheap no-op when no building overlaps it. */
	void RefreshBuildingPadsForChunk(ATerraDyneChunk* Chunk);

	/** World-space XY bounds of a chunk. */
	FBox2D GetChunkBoundsXY(const ATerraDyneChunk* Chunk) const;

	/** Returns the chunk at the given grid coordinate, or nullptr. */
	ATerraDyneChunk* GetChunkAtCoord(FIntPoint Coord) const;

	void RegisterChunk(ATerraDyneChunk* Chunk);
	void UnregisterChunk(ATerraDyneChunk* Chunk);

	void BeginStroke(FVector WorldLocation, float Radius, APlayerController* Controller);
	void CommitStroke(APlayerController* Controller);
	void RestoreSnapshots(const FTerraDyneUndoEntry& Entry);
	void Undo(APlayerController* Controller);
	void Redo(APlayerController* Controller);

	/** Server → All Clients: broadcast a brush application so every client updates visuals. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyBrush(const FTerraDyneBrushParams& Params);

	/** Server → All Clients: broadcast global noise application to all clients. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyGlobalNoise(float Strength, float Frequency, float Seed);

	/** Server → All Clients: broadcast building pad refresh to all clients. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_RefreshBuildingPads(FVector2D MinXY, FVector2D MaxXY);


	/** Send every live chunk's state to one controller, to catch up a late joiner. Server only. */
	void SendFullSyncToController(ATerraDyneEditController* Controller);

	/**
	 * Server → All Clients: replay one chunk's sculpt state after an undo/redo.
	 *
	 * Sent as sparse (index, value) pairs in batches rather than the whole buffer. A full sculpt
	 * buffer is Resolution^2 floats — 65,536 bytes at the default 128 — which overflows Unreal's
	 * 65,536-byte maximum bunch size by the RPC header alone, and the send is dropped with only an
	 * "Attempted to send bunch exceeding max allowed size" error on the server. The client never
	 * hears about it, so undo appeared to work on the host and do nothing everywhere else.
	 *
	 * Sculpt buffers are mostly zeroes (only edited ground is non-zero), so the sparse form is
	 * typically a tiny fraction of the raw array, and batching bounds the worst case.
	 *
	 * bFirstBatch clears the client's buffer before applying; bFinalBatch triggers the mesh rebuild.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SyncChunkSculpt(FIntPoint Coord, const TArray<int32>& Indices, const TArray<float>& Values, bool bFirstBatch, bool bFinalBatch);

	/** Splits a chunk's sculpt buffer into safely-sized sparse batches and multicasts them. */
	void SendChunkSculptSync(class ATerraDyneChunk* Chunk);

	/** Returns true if a stroke is pending for the given controller. */
	bool HasPendingStroke(APlayerController* Controller) const
	{
		return PendingStroke.IsSet() && PendingStrokeOwner == Controller;
	}

	/** Returns true if undo is available for this player. */
	bool CanUndo(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
		return Stack && Stack->Num() > 0;
	}

	/** Returns true if redo is available for this player. */
	bool CanRedo(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
		return Stack && Stack->Num() > 0;
	}

	/** Depth of undo stack for a player (for testing). */
	int32 GetUndoDepth(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
		return Stack ? Stack->Num() : 0;
	}

	/** Depth of redo stack for a player (for testing). */
	int32 GetRedoDepth(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
		return Stack ? Stack->Num() : 0;
	}

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void RebuildChunkMap();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void SaveWorld(FString SlotName = "TerraDyneSave");

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void LoadWorld(FString SlotName = "TerraDyneSave");

	// Same data as SaveWorld/LoadWorld, but written to/read from an explicit file path instead
	// of the engine's named SaveGame slot system — for exporting/importing a landscape as a
	// standalone file you can move, share, or pick from disk.
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	bool SaveWorldToFile(const FString& FilePath);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	bool LoadWorldFromFile(const FString& FilePath);

	class UTerraDyneSaveGame* BuildSaveGameInstance();
	void ApplyLoadedSaveGame(class UTerraDyneSaveGame* LoadInst, const FString& SourceLabel);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Migration")
	void SetAuthoredChunkCoordinates(const TArray<FIntPoint>& ChunkCoords, bool bWasImportedFromLandscape = true);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Framework")
	void ApplyWorldPreset(UTerraDyneWorldPreset* Preset = nullptr);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid RegisterPersistentActor(
		AActor* Actor,
		ETerraDynePopulationKind Kind = ETerraDynePopulationKind::Prop,
		bool bAllowRegrowth = false,
		float RegrowthDelaySeconds = 0.0f,
		bool bSnapToTerrain = false);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid PlacePersistentActorFromDescriptor(
		const FTerraDynePopulationDescriptor& Descriptor,
		const FTransform& WorldTransform,
		FName SourceRuleId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid PlacePersistentActor(
		TSubclassOf<AActor> ActorClass,
		const FTransform& WorldTransform,
		ETerraDynePopulationKind Kind = ETerraDynePopulationKind::RuntimePlaced,
		bool bSnapToTerrain = true,
		float TerrainOffset = 0.0f,
		bool bAllowRegrowth = false,
		float RegrowthDelaySeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	bool HarvestPersistentPopulation(const FGuid& PopulationId);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	bool SetPersistentPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	void RefreshPopulationForLoadedChunks();

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Population")
	int32 GetPersistentPopulationCount() const { return PersistentPopulationEntries.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Procedural")
	FName GetBiomeTagAtLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Gameplay")
	bool CanBuildAtLocation(FVector WorldLocation, FString& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneAISpawnZone> GetAISpawnZonesAtLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|PCG")
	TArray<FTerraDynePCGPoint> GetPCGSeedPointsForChunk(
		FIntPoint Coord,
		bool bIncludePopulation = true,
		bool bIncludeAISpawnZones = true) const;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneTerrainChangedDelegate OnTerrainChanged;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneFoliageChangedDelegate OnFoliageChanged;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDynePopulationChangedDelegate OnPopulationChanged;

	/** When true, streaming is frozen — no chunks are loaded or unloaded. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "TerraDyne|Streaming")
	bool bStreamingPaused = false;

	//--- STATS ---
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	int32 GetActiveChunkCount() const { return ActiveChunkMap.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	int32 GetTotalVertexCount() const;

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	FTerraDyneGPUStats GetGPUStats() const;

	//--- EDITOR TOOLS ---
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne|Tools")
	void ManualImport();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne|Migration")
	void MigrateLandscapeProject();

	void ImportFromLandscape(ALandscapeProxy* TargetLandscape, bool bHideSource = true);
	void ImportFromLandscapeWithOptions(ALandscapeProxy* TargetLandscape, const FTerraDyneLandscapeMigrationOptions& Options);
	void ResampleLandscapeData(ATerraDyneChunk* Chunk, ULandscapeComponent* SourceComponent);
	void ImportInternal(ALandscapeProxy* Source, const FTerraDyneLandscapeMigrationOptions& Options);
#endif

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_MasterMaterial();

	UFUNCTION()
	void OnRep_LandscapeMigrationState();

	UFUNCTION()
	void OnRep_FrameworkConfig();

private:
	UPROPERTY(Transient)
	TMap<FIntPoint, ATerraDyneChunk*> ActiveChunkMap;

	/** Live buildings, for pad rebuilds. Weak so a destroyed building needs no explicit cleanup. */
	TArray<TWeakObjectPtr<class ATerraDyneBuildingActor>> RegisteredBuildings;

	bool bMaterialsLoaded = false;
	float LODTimer = 0.25f;

	// --- Streaming ---
	// Streaming iterates all player controllers and computes the union of load regions.
	TSet<FIntPoint> PendingLoadQueue;
	TSet<FIntPoint> PendingUnloadQueue;
	TSet<FIntPoint> DirtyChunkSet;
	TSet<FIntPoint> ImportedChunkCoords;
	TMap<FGuid, int32> PopulationEntryIndexById;
	TMap<FIntPoint, int32> ProceduralChunkStateIndexByCoord;
	TMap<AActor*, FGuid> PopulationIdByActor;
	TMap<FGuid, TWeakObjectPtr<AActor>> PopulationActorById;
	TSet<FGuid> PopulationDestroyInProgress;
	uint32 LastStreamingHash = 0;
	float PopulationMaintenanceTimer = 0.0f;

	FIntPoint WorldToChunkCoord(const FVector& WorldPos) const;
	void UpdateStreaming(const TArray<FVector>& PlayerPositions);
	void ProcessStreamingQueues(const TArray<FVector>& PlayerPositions);
	void LoadOrSpawnChunk(FIntPoint Coord);
	void UnloadChunk(FIntPoint Coord);
	void MarkChunkDirty(FIntPoint Coord);
	ATerraDyneChunk* SpawnConfiguredChunk(FIntPoint Coord, int32 OverrideResolution = INDEX_NONE);
	FString GetChunkCachePath(FIntPoint Coord) const;
	void SaveChunkToCache(FIntPoint Coord, const FTerraDyneChunkData& Data);
	bool LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData);
	void ApplyMaterialToActiveChunks();
	void ClearUndoRedoState();
	void DestroyTrackedPopulationActors();
	bool IsImportedChunkCoord(FIntPoint Coord) const;
	bool ShouldAllowChunkCoord(FIntPoint Coord) const;
	int32 GetDistanceFromAuthoredChunks(FIntPoint Coord) const;
	int32 MakeChunkSeed(FIntPoint Coord) const;
	void RebuildWorldStateIndices();
	void EnsureChunkFrameworkState(FIntPoint Coord, ATerraDyneChunk* Chunk);
	FTerraDyneProceduralChunkState* FindProceduralChunkState(FIntPoint Coord);
	const FTerraDyneProceduralChunkState* FindProceduralChunkState(FIntPoint Coord) const;
	FName ResolveBiomeForChunk(FIntPoint Coord, const ATerraDyneChunk* Chunk, TArray<FName>* OutOverlays = nullptr) const;
	void GeneratePopulationForChunk(FIntPoint Coord, ATerraDyneChunk* Chunk);
	void SyncPopulationForChunk(FIntPoint Coord);
	void DespawnPopulationForChunk(FIntPoint Coord);
	AActor* SpawnPopulationActor(const FTerraDynePersistentPopulationEntry& Entry);
	void BindPopulationActor(const FGuid& PopulationId, AActor* Actor);
	UFUNCTION()
	void HandlePopulationActorDestroyed(AActor* DestroyedActor);
	void SetPopulationStateInternal(const FGuid& PopulationId, ETerraDynePopulationState NewState, FName Reason);
	void TickPopulationState(float DeltaTime);
	FTerraDynePopulationDescriptor MakeDescriptorFromActor(
		AActor* Actor,
		ETerraDynePopulationKind Kind,
		bool bAllowRegrowth,
		float RegrowthDelaySeconds,
		bool bSnapToTerrain) const;
	void BroadcastTerrainChanged(const TArray<FIntPoint>& ChangedCoords);
	void BroadcastFoliageChanged(const TArray<FIntPoint>& ChangedCoords);
	void BroadcastPopulationChanged(
		const FTerraDynePersistentPopulationEntry& Entry,
		FName Reason);
	void RefreshNavigationForBounds(const FBox& Bounds, bool bPopulationChange = false);
	float GetSlopeDegreesAtLocation(FVector WorldLocation) const;

	// --- Undo/Redo ---
	// NOTE: Undo/redo entries referencing unloaded (streamed-out) chunks are silently skipped.
	// A future improvement could persist undo snapshots alongside chunk cache data.
	TMap<APlayerController*, TArray<FTerraDyneUndoEntry>> UndoStacks;
	TMap<APlayerController*, TArray<FTerraDyneUndoEntry>> RedoStacks;
	TOptional<FTerraDyneUndoEntry> PendingStroke;
	APlayerController* PendingStrokeOwner = nullptr;

	void LoadMaterials();
	void SetupLighting();
	void SpawnDefaultSandboxChunk();
};
