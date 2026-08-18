// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/TerraDyneSaveGame.h"
#include "World/TerraDyneTileData.h"
#include "Building/TerraDyneGizmoActor.h"
#include "TerraDyneEditController.generated.h"

class ATerraDyneManager;

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

	//--- Building Requests ---
	void RequestPlaceBuilding(TSubclassOf<class ATerraDyneBuildingActor> BuildingClass, const FTransform& WorldTransform, int32 FloorCount);
	void RequestMoveBuilding(class ATerraDyneBuildingActor* Building, const FTransform& NewTransform);
	void RequestSetBuildingFloorCount(class ATerraDyneBuildingActor* Building, int32 NewFloorCount);
	void RequestSetBuildingSplinePoints(class ATerraDyneBuildingActor* Building, const TArray<FVector>& NewPoints);
	void RequestAddSplinePoint(class ATerraDyneBuildingActor* Building);
	void RequestRemoveSplinePoint(class ATerraDyneBuildingActor* Building, int32 PointIndex = INDEX_NONE);
	void RequestResetFootprint(class ATerraDyneBuildingActor* Building);
	void RequestRelevelBuilding(class ATerraDyneBuildingActor* Building);
	void RequestDeleteBuilding(class ATerraDyneBuildingActor* Building);

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
	TObjectPtr<class ATerraDyneGizmoActor> ActiveMoveGizmo;

	void UpdateHolographicPreview();
	void UpdateMoveGizmo();
	bool TraceGizmoUnderCursor(ETerraDyneGizmoAxis& OutAxis, FVector& OutHitPoint) const;
	bool TraceSplinePointUnderCursor(int32& OutPointIndex, FVector& OutHitPoint) const;
	bool IntersectCursorWithPlane(const FPlane& Plane, FVector& OutIntersection) const;

	bool bIsClicking = false;
	bool bStrokeBegun = false;
	bool bFlattenHeightLocked = false;
	float LockedFlattenHeight = 0.0f;

	ETerraDyneGizmoAxis DraggingGizmoAxis = ETerraDyneGizmoAxis::None;
	FVector GizmoDragStartBuildingLoc = FVector::ZeroVector;
	FVector GizmoDragStartIntersection = FVector::ZeroVector;
	FPlane GizmoDragPlane;
	FVector GizmoDragAxisDir = FVector::ZeroVector;

	int32 DraggingSplinePointIndex = INDEX_NONE;
	FVector SplinePointDragStartLocal = FVector::ZeroVector;

	float TimeSinceLastPadUpdate = 0.0f;
	mutable FVector2D LastMousePosition = FVector2D(-1.0f, -1.0f);

	void OnLeftClickStart();
	void OnLeftClickStop();
	void OnUndoPressed();
	void OnRedoPressed();
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

public:
	/**
	 * Road tool press and release.
	 *
	 * Virtual because road actors live in the game module, which this plugin cannot reference. The
	 * base implementations reach for them by name through reflection, which works but cannot see
	 * their real API; a subclass in the game module overrides these to call it directly.
	 */
	virtual void HandleRoadClick();

	/** Called on mouse release while a road tool is active. Nothing to release by default. */
	virtual void HandleRoadRelease() {}

protected:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> SelectedRoadActor;

	int32 SelectedRoadPointIndex = INDEX_NONE;

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
	void Server_HarvestPopulation(FGuid PopulationId);

	UFUNCTION(Server, Reliable)
	void Server_SetPopulationDestroyed(FGuid PopulationId, bool bDestroyed);

	UFUNCTION(Server, Reliable)
	void Server_SaveWorldToFile(const FString& FilePath);

	UFUNCTION(Server, Reliable)
	void Server_LoadWorldFromFile(const FString& FilePath);

	ATerraDyneManager* GetManager() const;

	/** Applies a brush on the authority and mirrors it to every other machine. */
	void ApplyAndBroadcast(const FTerraDyneBrushParams& Params);

	// Chunks can arrive from the server before streaming has spawned them locally. Hold the data
	// and retry each tick rather than dropping the sync.
	TMap<FIntPoint, FTerraDyneChunkData> PendingChunkSyncs;
};
