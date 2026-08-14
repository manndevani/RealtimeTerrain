// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditGameMode.h"
#include "Core/TerraDyneEditPawn.h"
#include "Core/TerraDyneEditController.h"

ATerraDyneEditGameMode::ATerraDyneEditGameMode()
{
	DefaultPawnClass = ATerraDyneEditPawn::StaticClass();
	PlayerControllerClass = ATerraDyneEditController::StaticClass();
}

APawn* ATerraDyneEditGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Space players out along X so a shared PlayerStart does not stack them inside one another.
	// The first player is unshifted; each subsequent one steps further along.
	FTransform SpawnAt = SpawnTransform;
	SpawnAt.AddToTranslation(FVector(static_cast<float>(SpawnedPawnCount) * 1500.0f, 0.0f, 0.0f));
	++SpawnedPawnCount;

	APawn* SpawnedPawn = GetWorld()->SpawnActor<APawn>(PawnClass, SpawnAt, SpawnInfo);
	if (!SpawnedPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyneEditGameMode: failed to spawn pawn for %s"),
			NewPlayer ? *NewPlayer->GetName() : TEXT("null controller"));
	}

	return SpawnedPawn;
}
