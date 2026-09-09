// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/TerraDyneSaveGame.h"
#include "World/TerraDyneTileData.h"
#include "IO/TerraDyneRasterLayers.h"
#include "IO/TerraDyneOSMIngest.h"
#include "Building/TerraDyneBuildingTypes.h"
#include "Building/TerraDyneModelTypes.h"
#include "TerraDyneEditController.generated.h"

class ATerraDyneManager;
class UGizmoManipulationSubsystem;

/**
 * What kind of thing an FTerraDyneActorUndoEntry recorded, so Undo/Redo know which direction is
 * "backward". Spawn: the action created actor(s) -- Undo destroys them, Redo re-creates them from
 * the snapshot taken right after they were spawned. Delete: the action removed actor(s) -- Undo
 * re-creates them from a snapshot taken just before the delete, Redo destroys them again.
 */
UENUM()
enum class ETerraDyneActorUndoOp : uint8
{
	Spawn,
	Delete
};

/** Which actor type (and therefore which data member) a snapshot describes. */
UENUM()
enum class ETerraDyneActorSnapshotKind : uint8
{
	Building,
	Model
};

/**
 * Enough to reconstruct, or to find and destroy, one actor a building/model/OSM operation touched.
 *
 * Deliberately reuses ATerraDyneBuildingActor::ToData()/FTerraDyneBuildingData and
 * ATerraDynePlacedModelActor::ToData()/FTerraDynePlacedModelData rather than inventing a lighter
 * snapshot -- those structs are already exactly "what's needed to reconstruct this actor" (they
 * were built for save-game persistence), and undoing a delete needs precisely that payload.
 */
USTRUCT()
struct FTerraDyneActorSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	ETerraDyneActorSnapshotKind Kind = ETerraDyneActorSnapshotKind::Building;

	UPROPERTY()
	FTerraDyneBuildingData BuildingData;

	UPROPERTY()
	FTerraDynePlacedModelData ModelData;

	/** Valid while the actor this snapshot describes currently exists in the world. */
	UPROPERTY()
	TWeakObjectPtr<AActor> LiveActor;
};

/**
 * One undoable building/model/OSM action. Actors.Num() is 1 for a single place/delete and N for an
 * OSM import's batch of buildings, so one Ctrl+Z reverses the whole import in one step rather than
 * needing N presses.
 */
USTRUCT()
struct FTerraDyneActorUndoEntry
{
	GENERATED_BODY()

	UPROPERTY()
	ETerraDyneActorUndoOp Op = ETerraDyneActorUndoOp::Spawn;

	UPROPERTY()
	TArray<FTerraDyneActorSnapshot> Actors;
};

/**
 * Network routing for terrain edits.
 *
 * Deliberately owns no input and no tool state — ATerraDyneOrchestrator handles the mouse and the
 * tool UI, and ATerraDyneEditPawn handles the camera. This class exists purely because a Server RPC
 * only routes from an actor the sending client owns, and the orchestrator is an unowned world actor.
 * Anything wanting to change terrain in a networked session has to come through here.
 *
 * Callers use the Request* helpers and stay agnostic about authority: on a listen server or in
 * standalone they apply immediately and multicast, and on a client they forward to the server, which
 * applies and multicasts. Terrain heights are never replicated as data — every machine regenerates
 * them from the same deterministic noise, so only the *edits* need to travel.
 */
UCLASS()
class TERRADYNE_API ATerraDyneEditController : public APlayerController
{
	GENERATED_BODY()

public:
	ATerraDyneEditController();

	/** Whether this controller has permission to edit terrain and place buildings. False by default for clients so only instructor edits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Permissions")
	bool bCanEditTerrain = false;

	/** Whether non-host clients should show the tool UI panel locally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|UI")
	bool bShowUIForClients = false;

	/** Tool panel spawned for the local player. */
	UPROPERTY(EditDefaultsOnly, Category = "TerraDyne|UI")
	TSubclassOf<class UUserWidget> UIClass;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Returns true if this controller can edit. True for local host (instructor) or any player
	 * when bCanEditTerrain is enabled.
	 */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Roles")
	bool IsInstructor() const;

	/**
	 * Server-side permission check — the counterpart to IsInstructor(), and the one every Server
	 * RPC is gated on.
	 *
	 * IsInstructor() cannot do this job. It requires IsLocalController(), which is false for a
	 * remote client's controller *on the server* — so as a Server RPC guard it would reject every
	 * client unconditionally while appearing to check a permission. This asks the question the
	 * authority can actually answer: the listen-server host owns a local controller and always
	 * passes; everyone else passes only on bCanEditTerrain, whose authoritative copy lives here.
	 *
	 * On a dedicated server no controller is local, so every player is governed by the flag.
	 */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Roles")
	bool HasEditAuthority() const;

	/** Open an undo snapshot covering everything the coming stroke touches. */
	void RequestBeginStroke(FVector WorldLocation, float Radius);

	/** Apply one brush dab. Safe to call every frame while dragging. */
	void RequestBrush(const FTerraDyneBrushParams& Params);

	/** Close the current stroke so it becomes a single undo entry. */
	void RequestCommitStroke();

	void RequestUndo();
	void RequestRedo();

	//--- Save / Load Requests ---
	void RequestSaveWorldToFile(const FString& FilePath);
	void RequestLoadWorldFromFile(const FString& FilePath);

	/** Runs Manager->ImportGeoTIFF(FilePath) on the server -- the client-facing entry point the
	 *  runtime file-picker widget calls, since ImportGeoTIFF itself only does anything on authority. */
	void RequestImportGeoTIFF(const FString& FilePath);

	/** Multi-layer raster import / unload / scene loading */
	void RequestImportRasterLayer(ETerraDyneRasterLayer Layer, const FString& FilePath);
	void RequestUnloadRasterLayer(ETerraDyneRasterLayer Layer);
	void RequestLoadSpecificRasterLayer(ETerraDyneRasterLayer Layer);
	void RequestUnloadSpecificRasterLayer(ETerraDyneRasterLayer Layer);
	void RequestUnloadAllRasterLayers();
	void RequestLoadRasterLayersIntoScene();

	/** Runs Manager->StartSandboxMode() on the server -- the client-facing entry point the runtime
	 *  world-mode-choice widget's "Sandbox" button calls. */
	void RequestStartSandboxMode();

	//--- Building Requests ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestPlaceBuilding(TSubclassOf<class ATerraDyneBuildingActor> BuildingClass, const FTransform& WorldTransform, int32 FloorCount);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestMoveBuilding(class ATerraDyneBuildingActor* Building, const FTransform& NewTransform);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestSetBuildingFloorCount(class ATerraDyneBuildingActor* Building, int32 NewFloorCount);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestSetBuildingSplinePoints(class ATerraDyneBuildingActor* Building, const TArray<FVector>& NewPoints);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestAddSplinePoint(class ATerraDyneBuildingActor* Building);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestRemoveSplinePoint(class ATerraDyneBuildingActor* Building, int32 PointIndex = -1);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestResetFootprint(class ATerraDyneBuildingActor* Building);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestRelevelBuilding(class ATerraDyneBuildingActor* Building);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Building")
	void RequestDeleteBuilding(class ATerraDyneBuildingActor* Building);

	//--- Placed 3D Model Requests ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Model")
	void RequestPlaceModel(const FGuid& AssetId, const FTransform& WorldTransform);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Model")
	void RequestMoveModel(class ATerraDynePlacedModelActor* ModelActor, const FTransform& NewTransform);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Model")
	void RequestDeleteModel(class ATerraDynePlacedModelActor* ModelActor);

	void SelectModelActor(class ATerraDynePlacedModelActor* ModelActor);
	void ClearModelSelection();

	//--- OpenStreetMap ingestion ---

	/**
	 * Imports a .osm document, spawning buildings for the ways it contains. Routed through
	 * the server like every other world edit; the summary of the last run is left in
	 * LastOSMImportSummary for the panel to display.
	 */
	void RequestImportOSM(const FString& FilePath, bool bImportBuildings);

	/** Result of the most recent (or in-progress) OSM import, for UI display. */
	struct FTerraDyneOSMImportSummary GetLastOSMImportSummary() const;

	/**
	 * Mirrors ATerraDyneManager::GetGeoTIFFLoadProgress's shape exactly: OutDone/OutTotal are the
	 * spawn queue's progress, return value is the fraction (1.0f when nothing is queued -- "done").
	 * An OSM import queues its actual spawns and drains them a few per Tick rather than spawning
	 * everything in one call, so a large extract (a city-sized document can be thousands of ways)
	 * does not hitch the game thread the way GeoTIFF decode does today.
	 */
	float GetOSMImportProgress(int32& OutDone, int32& OutTotal) const;

	/** True while a queued OSM import still has spawns left to drain. */
	bool IsOSMImportInProgress() const { return PendingOSMSpawnQueue.Num() > 0; }

	/**
	 * Drains up to MaxOSMFeaturesPerTick queued spawns; a no-op when nothing is queued or off
	 * authority. Tick() (protected, called by the engine's tick manager) is this function's only
	 * real caller -- exposed here as well because automation tests spawn a bare
	 * ATerraDyneEditController with FAutomationEditorCommonUtils::CreateNewMap(), which never
	 * actually runs a frame loop, so a test has no other way to pump the drain forward.
	 */
	void ProcessOSMSpawnQueue();

	//--- Population Requests ---
	void RequestHarvestPopulation(const FGuid& PopulationId);
	void RequestSetPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed);

	/** Server -> owning client: one chunk's full state, used to catch up a late joiner. */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChunkSync(const FTerraDyneChunkData& Data);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

private:
	// Mouse sculpting lives here, not on ATerraDyneOrchestrator, because the orchestrator is only
	// spawned on the authority — a client had no orchestrator, so no tool UI and no input at all,
	// and nothing on that machine ever asked the server to edit anything. The controller exists on
	// every machine, so this is the one place the input can work for host and client alike.
	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> ActiveUI;

	UPROPERTY(Transient)
	TObjectPtr<class ATerraDyneBuildingActor> HolographicGhostBuilding;

	UPROPERTY(Transient)
	TObjectPtr<class ATerraDynePlacedModelActor> HolographicGhostModel;

	UPROPERTY(Transient)
	TObjectPtr<class ATerraDyneGizmoActor> ActiveMoveGizmo;

	/** Lazily fetched the first time UpdateMoveGizmo runs. */
	UPROPERTY(Transient)
	TObjectPtr<UGizmoManipulationSubsystem> GizmoSubsystem;

	TWeakObjectPtr<class ATerraDyneBuildingActor> GizmoBoundBuilding;
	TWeakObjectPtr<class ATerraDynePlacedModelActor> GizmoBoundModel;

	void UpdateHolographicPreview();
	void UpdateHolographicModelPreview();
	void UpdateMoveGizmo();
	bool TraceSplinePointUnderCursor(int32& OutPointIndex, FVector& OutHitPoint) const;
	bool IntersectCursorWithPlane(const FPlane& Plane, FVector& OutIntersection) const;

	/** Bound to GizmoSubsystem->OnManipulationCancel -- reacts only if the cancelled target is our own bound building (the subsystem is shared with the road tool). */
	void HandleGizmoManipulationCancelled(USceneComponent* CancelledTarget);

	/** Builds an FGizmoInteractionRay from the current cursor position, or returns false if the mouse can't be deprojected right now. */
	bool BuildGizmoInteractionRay(struct FGizmoInteractionRay& OutRay) const;

	/** The active camera's forward vector, for hit-testing handles whose plane depends on view direction (e.g. the screen-space rotation ring). */
	FVector GetCameraForwardVector() const;

	bool bIsClicking = false;
	bool bStrokeBegun = false;
	bool bFlattenHeightLocked = false;
	float LockedFlattenHeight = 0.0f;

	/** Whether the drag currently in progress is the shared gizmo (as opposed to a plain spline-vertex handle). BeginManipulation can return None, so this can't just be inferred from GizmoSubsystem->IsManipulating(). */
	bool bIsGizmoAxisDrag = false;
	FVector GizmoDragStartIntersection = FVector::ZeroVector;
	FPlane GizmoDragPlane;

	int32 DraggingSplinePointIndex = INDEX_NONE;
	FVector SplinePointDragStartLocal = FVector::ZeroVector;

	float TimeSinceLastPadUpdate = 0.0f;
	mutable FVector2D LastMousePosition = FVector2D(-1.0f, -1.0f);

	void OnLeftClickStart();
	void OnLeftClickStop();
	void OnUndoPressed();
	void OnRedoPressed();
	void OnCancelManipulationPressed();

	//--- Building/model/OSM actor undo ---
	//
	// A separate stack alongside terrain's UndoStacks (owned by ATerraDyneManager, keyed by
	// controller because one Manager singleton serves every player). This one needs no such map:
	// ATerraDyneEditController already exists once per player -- both on the owning client and,
	// authoritatively, on the server -- so `this` already IS the per-player scope terrain's map
	// exists to fake. Composed into OnUndoPressed/OnRedoPressed by priority, per the ground rule
	// that no existing terrain undo behavior changes as a side effect of this.
	static constexpr int32 MaxActorUndoHistory = 20; // matches ATerraDyneManager's MaxUndoHistory default

	UPROPERTY(Transient)
	TArray<FTerraDyneActorUndoEntry> ActorUndoStack;

	UPROPERTY(Transient)
	TArray<FTerraDyneActorUndoEntry> ActorRedoStack;

	void PushActorUndoEntry(FTerraDyneActorUndoEntry&& Entry);

	/** Undo of a Spawn, or redo of a Delete: destroys every live actor a snapshot still points to. */
	void DestroyActorsInEntry(FTerraDyneActorUndoEntry& Entry);

	/** Undo of a Delete, or redo of a Spawn: reconstructs every actor from its snapshot data. */
	void RespawnActorsInEntry(FTerraDyneActorUndoEntry& Entry);

	/** Shift+W/E/R -- switches the shared gizmo's mode. One binding serves both consumers, same as OnCancelManipulationPressed. */
	void OnSetGizmoModeTranslate();
	void OnSetGizmoModeRotate();
	void OnSetGizmoModeScale();

	/** C -- toggles the shared gizmo between World and Local coordinate space. */
	void OnToggleGizmoSpacePressed();

	/** G -- toggles the shared gizmo's persistent snapping setting. Holding Ctrl during a drag inverts this for that drag only (see Tick's gizmo-drag branch) without touching the persistent value. */
	void OnToggleGizmoSnappingPressed();

	void PerformToolAction();

	/**
	 * Building tools act once per press, not once per frame like the sculpt brushes — placing a
	 * building every tick while the button is held would carpet the terrain.
	 *
	 * Selection is client-local and placement only ever runs here on the authority: the tool panel
	 * is built solely for IsInstructor() (local AND authoritative), so there is no client path to
	 * route. Replicating placed buildings out to other machines is phase 6.
	 */
	void HandleBuildingClick();
	void HandleModelClick();

public:
	/**
	 * Building/model/OSM actor undo/redo. Public so OnUndoPressed/OnRedoPressed (private, chains
	 * this before the terrain stack) can call it, and so a test can drive undo/redo directly
	 * without needing input simulation.
	 */
	bool TryActorUndo();
	bool TryActorRedo();

protected:
	/** Traces the cursor against terrain and buildings. Returns false when nothing was hit. */
	bool TraceUnderCursor(FHitResult& OutHit) const;

	class UTerraDyneToolWidget* GetToolWidget() const;

private:

	UFUNCTION(Server, Reliable)
	void Server_BeginStroke(FVector WorldLocation, float Radius);

	UFUNCTION(Server, Reliable)
	void Server_ApplyBrush(const FTerraDyneBrushParams& Params);

	UFUNCTION(Server, Reliable)
	void Server_CommitStroke();

	UFUNCTION(Server, Reliable)
	void Server_Undo();

	UFUNCTION(Server, Reliable)
	void Server_Redo();

	UFUNCTION(Server, Reliable)
	void Server_PlaceBuilding(TSubclassOf<class ATerraDyneBuildingActor> BuildingClass, FTransform WorldTransform, int32 FloorCount);

	UFUNCTION(Server, Reliable)
	void Server_MoveBuilding(class ATerraDyneBuildingActor* Building, FTransform NewTransform);

	UFUNCTION(Server, Reliable)
	void Server_SetBuildingFloorCount(class ATerraDyneBuildingActor* Building, int32 NewFloorCount);

	UFUNCTION(Server, Reliable)
	void Server_SetBuildingSplinePoints(class ATerraDyneBuildingActor* Building, const TArray<FVector>& NewPoints);

	UFUNCTION(Server, Reliable)
	void Server_AddSplinePoint(class ATerraDyneBuildingActor* Building);

	UFUNCTION(Server, Reliable)
	void Server_RemoveSplinePoint(class ATerraDyneBuildingActor* Building, int32 PointIndex);

	UFUNCTION(Server, Reliable)
	void Server_ResetFootprint(class ATerraDyneBuildingActor* Building);

	UFUNCTION(Server, Reliable)
	void Server_RelevelBuilding(class ATerraDyneBuildingActor* Building);

	UFUNCTION(Server, Reliable)
	void Server_DeleteBuilding(class ATerraDyneBuildingActor* Building);

	UFUNCTION(Server, Reliable)
	void Server_PlaceModel(FGuid AssetId, FTransform WorldTransform);

	UFUNCTION(Server, Reliable)
	void Server_MoveModel(class ATerraDynePlacedModelActor* ModelActor, FTransform NewTransform);

	UFUNCTION(Server, Reliable)
	void Server_DeleteModel(class ATerraDynePlacedModelActor* ModelActor);

	UFUNCTION(Server, Reliable)
	void Server_HarvestPopulation(FGuid PopulationId);

	UFUNCTION(Server, Reliable)
	void Server_SetPopulationDestroyed(FGuid PopulationId, bool bDestroyed);

	UFUNCTION(Server, Reliable)
	void Server_SaveWorldToFile(const FString& FilePath);

	UFUNCTION(Server, Reliable)
	void Server_LoadWorldFromFile(const FString& FilePath);

	UFUNCTION(Server, Reliable)
	void Server_ImportGeoTIFF(const FString& FilePath);

	UFUNCTION(Server, Reliable)
	void Server_ImportRasterLayer(ETerraDyneRasterLayer Layer, const FString& FilePath);

	UFUNCTION(Server, Reliable)
	void Server_UnloadRasterLayer(ETerraDyneRasterLayer Layer);

	UFUNCTION(Server, Reliable)
	void Server_UnloadAllRasterLayers();

	UFUNCTION(Server, Reliable)
	void Server_LoadRasterLayersIntoScene();

	UFUNCTION(Server, Reliable)
	void Server_LoadSpecificRasterLayer(ETerraDyneRasterLayer Layer);

	UFUNCTION(Server, Reliable)
	void Server_UnloadSpecificRasterLayer(ETerraDyneRasterLayer Layer);

	UFUNCTION(Server, Reliable)
	void Server_StartSandboxMode();

	UFUNCTION(Server, Reliable)
	void Server_ImportOSM(const FString& FilePath, bool bImportBuildings);

	/**
	 * One already-fully-resolved OSM feature waiting to be spawned. All the CPU-only work --
	 * parsing, classification, lon/lat -> world conversion, footprint winding/centroid -- happens
	 * synchronously up front in ImportOSMInternal, because none of it is what hitches the game
	 * thread. Only the engine-side cost (SpawnActor, wall/mesh generation, terrain-height sampling)
	 * is deferred to the ticked drain below.
	 */
	struct FTerraDyneOSMSpawnJob
	{
		FVector Origin = FVector::ZeroVector;
		TArray<FVector> LocalFootprint;
		int32 Levels = 2;
	};

	/** Starts (synchronously resolves and queues) an OSM import; the actual spawns drain from Tick. */
	FTerraDyneOSMImportSummary ImportOSMInternal(const FString& FilePath, const FTerraDyneOSMImportOptions& Options);

	static constexpr int32 MaxOSMFeaturesPerTick = 8;
	static constexpr double MaxOSMMillisecondsPerTick = 4.0;

	TArray<FTerraDyneOSMSpawnJob> PendingOSMSpawnQueue;
	int32 OSMImportTotalJobs = 0;

	/** Accumulates as PendingOSMSpawnQueue drains; finalized into LastOSMImportSummary and pushed as one undo entry once the queue empties. */
	FTerraDyneOSMImportSummary InProgressOSMSummary;
	FTerraDyneActorUndoEntry InProgressOSMUndoEntry;
	FString InProgressOSMFilePath;

	FTerraDyneOSMImportSummary LastOSMImportSummary;

	ATerraDyneManager* GetManager() const;

	/** Applies a brush on the authority and mirrors it to every other machine. */
	void ApplyAndBroadcast(const FTerraDyneBrushParams& Params);

	// Chunks can arrive from the server before streaming has spawned them locally. Hold the data
	// and retry each tick rather than dropping the sync.
	TMap<FIntPoint, FTerraDyneChunkData> PendingChunkSyncs;
};
