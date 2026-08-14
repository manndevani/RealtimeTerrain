// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneSceneSetup.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneWorldPreset.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "CelestialVaultDaySequenceActor.h"
#include "DaySequenceCollectionAsset.h"

ATerraDyneSceneSetup::ATerraDyneSceneSetup()
{
	PrimaryActorTick.bCanEverTick = false;
	ManagerClass = ATerraDyneManager::StaticClass();
}

void ATerraDyneSceneSetup::InitializeWorld()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World) return;

	UE_LOG(LogTemp, Log, TEXT("TerraDyne Wizard: Starting One-Click Showcase Setup..."));

	// 1. Clean old Managers/Orchestrators/Chunks. Chunks are spawned separately by the Manager
	// (not owned/destroyed with it), so without this an old Manager's chunks were left orphaned
	// in the level on every re-run — stacking stale, overlapping terrain meshes underneath the
	// freshly generated ones.
	TArray<AActor*> OldActors;
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneManager::StaticClass(), OldActors);
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneOrchestrator::StaticClass(), OldActors);
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneChunk::StaticClass(), OldActors);
	for (AActor* Act : OldActors) Act->Destroy();

	// 2. Spawn Manager at Origin
	FActorSpawnParameters Params;
	UClass* ClassToSpawn = ManagerClass.Get() ? ManagerClass.Get() : ATerraDyneManager::StaticClass();

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>(
		ClassToSpawn,
		FVector::ZeroVector, FRotator::ZeroRotator, Params
	);

	if (Manager)
	{
		Manager->SetActorLabel(TEXT("TerraDyne_System_Manager"));

		Manager->ActiveLayer = ETerraDyneLayer::Sculpt;
		Manager->GlobalChunkSize = 10000.0f;
		Manager->WorldPreset = WorldPreset;
		if (WorldPreset)
		{
			Manager->ApplyWorldPreset(WorldPreset);
		}

		switch (DemoTemplate)
		{
		case ETerraDyneDemoTemplate::FullFeatureShowcase:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;

		case ETerraDyneDemoTemplate::SurvivalFramework:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;

		case ETerraDyneDemoTemplate::AuthoredWorldConversion:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;

		case ETerraDyneDemoTemplate::Sandbox:
		default:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = false;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;
		}

		Manager->RebuildChunkMap();
	}

	// 3. Spawn Orchestrator (The Showcase Director)
	ATerraDyneOrchestrator* Director = World->SpawnActor<ATerraDyneOrchestrator>(
		ATerraDyneOrchestrator::StaticClass(),
		FVector(0,0,1000), FRotator::ZeroRotator
	);
	if (Director)
	{
		Director->SetActorLabel(TEXT("TerraDyne_Showcase_Director"));
		// The scripted ~95s showcase no longer auto-runs on Play for any template — go straight
		// to Interactive so sculpting/tool input works immediately. "Run Full Showcase" in the
		// panel still works to trigger it manually.
		Director->bStartInInteractivePhase = true;
	}

	// 4. Setup Spectacular Lighting — CelestialVault gives a physically-accurate sun/moon/sky
	// (SkyAtmosphere, ExponentialHeightFog, real ephemeris-driven sun) in one actor, replacing
	// the old flat DirectionalLight+SkyLight-only setup that had no atmosphere or fog at all.
	if (!UGameplayStatics::GetActorOfClass(World, ACelestialVaultDaySequenceActor::StaticClass()))
	{
		ACelestialVaultDaySequenceActor* Vault = World->SpawnActor<ACelestialVaultDaySequenceActor>(
			ACelestialVaultDaySequenceActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Vault)
		{
			Vault->SetActorLabel(TEXT("TerraDyne_CelestialVault"));
			if (UDaySequenceCollectionAsset* Collection = LoadObject<UDaySequenceCollectionAsset>(
				nullptr, TEXT("/CelestialVault/DSCA_CelestialVault.DSCA_CelestialVault")))
			{
				Vault->DaySequenceCollections.Add(Collection);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SHOWCASE READY: Just press PLAY to watch the feature tour."));
#endif
}
