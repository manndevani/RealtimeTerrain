// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GRTEditGameMode.generated.h"

/**
 * Spawns AGRTEditPawn as the default pawn so terrain editing gets the pinned-camera input
 * scheme from the first frame (left drag edits, right drag orbits).
 *
 * PlayerControllerClass is deliberately left as the engine default: AGRTOrchestrator binds its
 * own sculpt input onto GetPlayerController(0) and drives the tool UI, so no bespoke controller is
 * needed.
 */
UCLASS()
class GRT_API AGRTEditGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGRTEditGameMode();

	/**
	 * The map ships a single PlayerStart, so every player is placed on the same spot. The default
	 * collision handling refuses to put a second pawn inside the first, which left player 2 with a
	 * controller and no pawn — and therefore no camera, no cursor trace and no ability to sculpt.
	 * Force the spawn through and fan players out so they do not start co-located.
	 */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

private:
	/** Pawns spawned so far, used only to fan players out from the shared PlayerStart. */
	int32 SpawnedPawnCount = 0;
};
