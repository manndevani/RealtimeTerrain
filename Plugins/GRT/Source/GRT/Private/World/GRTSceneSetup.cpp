// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/GRTSceneSetup.h"
#include "Core/GRTManager.h"
#include "Core/GRTWorldPreset.h"
#include "World/GRTChunk.h"
#include "World/GRTOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "CelestialVaultDaySequenceActor.h"
#include "DaySequenceCollectionAsset.h"

AGRTSceneSetup::AGRTSceneSetup()
{
	PrimaryActorTick.bCanEverTick = false;
	ManagerClass = AGRTManager::StaticClass();
}

void AGRTSceneSetup::InitializeWorld()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World) return;

	UE_LOG(LogTemp, Log, TEXT("GRT Wizard: Starting One-Click Showcase Setup..."));

	// 1. Clean old Managers/Orchestrators/Chunks. Chunks are spawned separately by the Manager
	// (not owned/destroyed with it), so without this an old Manager's chunks were left orphaned
	// in the level on every re-run — stacking stale, overlapping terrain meshes underneath the
	// freshly generated ones.
	TArray<AActor*> OldActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTManager::StaticClass(), OldActors);
	UGameplayStatics::GetAllActorsOfClass(World, AGRTOrchestrator::StaticClass(), OldActors);
	UGameplayStatics::GetAllActorsOfClass(World, AGRTChunk::StaticClass(), OldActors);
	for (AActor* Act : OldActors) Act->Destroy();

	// 2. Spawn Manager at Origin
	FActorSpawnParameters Params;
	UClass* ClassToSpawn = ManagerClass.Get() ? ManagerClass.Get() : AGRTManager::StaticClass();

	AGRTManager* Manager = World->SpawnActor<AGRTManager>(
		ClassToSpawn,
		FVector::ZeroVector, FRotator::ZeroRotator, Params
	);

	if (Manager)
	{
		Manager->SetActorLabel(TEXT("GRT_System_Manager"));

		Manager->ActiveLayer = EGRTLayer::Sculpt;
		Manager->GlobalChunkSize = 10000.0f;
		Manager->WorldPreset = WorldPreset;
		if (WorldPreset)
		{
			Manager->ApplyWorldPreset(WorldPreset);
		}

		switch (DemoTemplate)
		{
		case EGRTDemoTemplate::SurvivalFramework:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			break;

		case EGRTDemoTemplate::AuthoredWorldConversion:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			break;

		case EGRTDemoTemplate::Sandbox:
		default:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = false;
			break;
		}

		Manager->RebuildChunkMap();
	}

	// 3. Setup Lighting — CelestialVault
	if (!UGameplayStatics::GetActorOfClass(World, ACelestialVaultDaySequenceActor::StaticClass()))
	{
		ACelestialVaultDaySequenceActor* Vault = World->SpawnActor<ACelestialVaultDaySequenceActor>(
			ACelestialVaultDaySequenceActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Vault)
		{
			Vault->SetActorLabel(TEXT("GRT_CelestialVault"));
			if (UDaySequenceCollectionAsset* Collection = LoadObject<UDaySequenceCollectionAsset>(
				nullptr, TEXT("/CelestialVault/DSCA_CelestialVault.DSCA_CelestialVault")))
			{
				Vault->DaySequenceCollections.Add(Collection);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("GRT: Scene setup completed successfully."));
#endif
}
