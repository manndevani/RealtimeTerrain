// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTManager.h"
#include "World/GRTChunk.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGRTPersistentPopulationSaveLoadTest,
	"GRT.WorldFramework.PersistentPopulationSaveLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTPersistentPopulationSaveLoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	AGRTManager* Manager = World->SpawnActor<AGRTManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 1000.0f;

	AGRTChunk* Chunk = World->SpawnActor<AGRTChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 1000.0f;
	Chunk->ChunkSizeWorldUnits = 1000.0f;
	Chunk->Initialize(16, 1000.0f);
	Manager->RebuildChunkMap();

	FGRTPopulationDescriptor Descriptor;
	Descriptor.Kind = EGRTPopulationKind::Harvestable;
	Descriptor.TypeId = TEXT("BerryBush");
	Descriptor.ActorClass = AStaticMeshActor::StaticClass();
	Descriptor.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Descriptor.bReplicates = true;
	Descriptor.bSnapToTerrain = true;
	Descriptor.bAllowRegrowth = true;
	Descriptor.RegrowthDelaySeconds = 5.0f;
	Descriptor.TerrainOffset = 25.0f;

	const FGuid PopulationId = Manager->PlacePersistentActorFromDescriptor(
		Descriptor,
		FTransform(FVector(0.0f, 0.0f, 250.0f)));
	TestTrue("Population id should be valid", PopulationId.IsValid());
	TestEqual("Persistent population count after placement", Manager->GetPersistentPopulationCount(), 1);

	TestTrue("Harvest should succeed", Manager->HarvestPersistentPopulation(PopulationId));
	TestEqual(
		"Population enters regrowing state",
		Manager->PersistentPopulationEntries[0].State,
		EGRTPopulationState::Regrowing);

	const FString SlotName = FString::Printf(
		TEXT("GRTWorldFramework_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Manager->SaveWorld(SlotName);

	Manager->LoadWorld(SlotName);

	TestEqual("Persistent population count restored", Manager->GetPersistentPopulationCount(), 1);
	TestEqual(
		"Population state restored",
		Manager->PersistentPopulationEntries[0].State,
		EGRTPopulationState::Regrowing);
	TestEqual(
		"Population type restored",
		Manager->PersistentPopulationEntries[0].Descriptor.TypeId,
		FName(TEXT("BerryBush")));

	const FGuid RestoredPopulationId = Manager->PersistentPopulationEntries[0].PopulationId;
	TestTrue("Restored population can be reactivated", Manager->SetPersistentPopulationDestroyed(RestoredPopulationId, false));
	TestEqual(
		"Population state becomes active again",
		Manager->PersistentPopulationEntries[0].State,
		EGRTPopulationState::Active);

	const TArray<FGRTPCGPoint> PopulationPoints =
		Manager->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, true, false);
	TestEqual("PCG exports one active population point", PopulationPoints.Num(), 1);

	UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGRTGameplayHooksAndProceduralMetadataTest,
	"GRT.WorldFramework.GameplayHooksAndProceduralMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTGameplayHooksAndProceduralMetadataTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	AGRTManager* Manager = World->SpawnActor<AGRTManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 1000.0f;
	Manager->ProceduralWorldSettings.WorldSeed = 42;
	Manager->ProceduralWorldSettings.bEnableInfiniteEdgeGrowth = true;

	FGRTBiomeOverlay Overlay;
	Overlay.BiomeTag = TEXT("Forest");
	Overlay.bApplyToProceduralChunks = true;
	Overlay.ProceduralNoiseMin = 0.0f;
	Overlay.ProceduralNoiseMax = 1.0f;
	Overlay.Priority = 5;
	Manager->BiomeOverlays.Add(Overlay);

	FGRTAISpawnZone SpawnZone;
	SpawnZone.ZoneId = TEXT("ForestSpawn");
	SpawnZone.RequiredBiomeTag = TEXT("Forest");
	SpawnZone.LocalBounds = FBox(FVector(-250.0f, -250.0f, -200.0f), FVector(250.0f, 250.0f, 500.0f));
	Manager->AISpawnZones.Add(SpawnZone);

	FGRTBuildPermissionZone BuildZone;
	BuildZone.ZoneId = TEXT("TownCore");
	BuildZone.Permission = EGRTBuildPermission::Blocked;
	BuildZone.Reason = TEXT("Protected settlement footprint");
	BuildZone.LocalBounds = FBox(FVector(-300.0f, -300.0f, -500.0f), FVector(300.0f, 300.0f, 500.0f));
	Manager->BuildPermissionZones.Add(BuildZone);

	AGRTChunk* Chunk = World->SpawnActor<AGRTChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 1000.0f;
	Chunk->ChunkSizeWorldUnits = 1000.0f;
	Chunk->Initialize(16, 1000.0f);
	Manager->RebuildChunkMap();

	TestEqual("Chunk primary biome derived from overlay", Chunk->PrimaryBiomeTag, FName(TEXT("Forest")));

	FString BuildReason;
	TestFalse("Build permission should block location", Manager->CanBuildAtLocation(FVector::ZeroVector, BuildReason));
	TestTrue("Blocked build reason should be populated", !BuildReason.IsEmpty());

	const TArray<FGRTAISpawnZone> MatchingZones = Manager->GetAISpawnZonesAtLocation(FVector::ZeroVector);
	TestEqual("AI spawn zone query should match one zone", MatchingZones.Num(), 1);
	TestEqual("AI spawn zone id preserved", MatchingZones[0].ZoneId, FName(TEXT("ForestSpawn")));

	const TArray<FGRTPCGPoint> PCGPoints =
		Manager->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, false, true);
	TestEqual("PCG exports one AI zone point", PCGPoints.Num(), 1);
	TestEqual("PCG biome tag preserved", PCGPoints[0].BiomeTag, FName(TEXT("Forest")));

	return true;
}
#endif
