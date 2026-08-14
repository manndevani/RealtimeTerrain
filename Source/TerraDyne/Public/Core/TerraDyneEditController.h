// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/TerraDyneSaveGame.h"
#include "World/TerraDyneTileData.h"
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

	/** Tool panel spawned for the local player. */
	UPROPERTY(EditDefaultsOnly, Category = "TerraDyne|UI")
	TSubclassOf<class UUserWidget> UIClass;

	/**
	 * Only the instructor gets the tool panel and sculpt input; everyone else is a viewer who sees
	 * the terrain change under them. Edits are applied on the authority and multicast, so a viewer
	 * needs no edit rights of its own — just a pawn to look through.
	 *
	 * The instructor is the player on the listen server (the host). To hand the role to a specific
	 * client instead, replace this with a replicated flag set by the game mode and rebuild the UI in
	 * its OnRep — the rest of the pipeline already works from any authority-capable controller.
	 */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Roles")
	bool IsInstructor() const;

	/** Open an undo snapshot covering everything the coming stroke touches. */
	void RequestBeginStroke(FVector WorldLocation, float Radius);

	/** Apply one brush dab. Safe to call every frame while dragging. */
	void RequestBrush(const FTerraDyneBrushParams& Params);

	/** Close the current stroke so it becomes a single undo entry. */
	void RequestCommitStroke();

	void RequestUndo();
	void RequestRedo();

	/** Server -> owning client: one chunk's full state, used to catch up a late joiner. */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChunkSync(const FTerraDyneChunkData& Data);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

private:
	// Mouse sculpting lives here, not on ATerraDyneOrchestrator, because the orchestrator is only
	// spawned on the authority — a client had no orchestrator, so no tool UI and no input at all,
	// and nothing on that machine ever asked the server to edit anything. The controller exists on
	// every machine, so this is the one place the input can work for host and client alike.
	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> ActiveUI;

	bool bIsClicking = false;
	bool bStrokeBegun = false;
	bool bFlattenHeightLocked = false;
	float LockedFlattenHeight = 0.0f;

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

	/** Traces the cursor against terrain and buildings. Returns false when nothing was hit. */
	bool TraceUnderCursor(FHitResult& OutHit) const;

	class UTerraDyneToolWidget* GetToolWidget() const;

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

	ATerraDyneManager* GetManager() const;

	/** Applies a brush on the authority and mirrors it to every other machine. */
	void ApplyAndBroadcast(const FTerraDyneBrushParams& Params);

	// Chunks can arrive from the server before streaming has spawned them locally. Hold the data
	// and retry each tick rather than dropping the sync.
	TMap<FIntPoint, FTerraDyneChunkData> PendingChunkSyncs;
};
