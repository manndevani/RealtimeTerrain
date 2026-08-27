// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneEditController.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Building/TerraDyneBuildingKit.h"
#include "World/TerraDyneChunk.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneBuildingReplicationTest,
	"TerraDyne.Multiplayer.BuildingReplicationAndSplineSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneBuildingReplicationTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneBuildingActor* Building = World->SpawnActor<ATerraDyneBuildingActor>();
	TestNotNull(TEXT("Building spawned"), Building);
	if (!Building)
	{
		return false;
	}

	// Verify replication configuration
	TestTrue(TEXT("Building actor has bReplicates enabled"), Building->GetIsReplicated());
	TestTrue(TEXT("Building actor is set to always relevant"), Building->bAlwaysRelevant);

	// Verify initial spline sync
	TestTrue(TEXT("Building has replicated spline points"), Building->ReplicatedSplinePoints.Num() >= 4);

	// Test floor count update
	Building->SetFloorCount(4);
	TestEqual(TEXT("Floor count updated to 4"), Building->FloorCount, 4);

	// Test reset to default footprint
	Building->ResetToDefaultFootprint();
	TestTrue(TEXT("Default footprint contains 4 points"), Building->ReplicatedSplinePoints.Num() == 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneBuildingPadSyncTest,
	"TerraDyne.Multiplayer.BuildingPadSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneBuildingPadSyncTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull(TEXT("Manager spawned"), Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 5000.0f;

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull(TEXT("Chunk spawned"), Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint(0, 0);
	Chunk->WorldSize = 5000.0f;
	UTerraDyneSubsystem* Subsystem = World->GetSubsystem<UTerraDyneSubsystem>();
	if (Subsystem)
	{
		Subsystem->RegisterManager(Manager);
	}

	Manager->RegisterChunk(Chunk);
	Manager->RebuildChunkMap();

	ATerraDyneBuildingActor* Building = World->SpawnActor<ATerraDyneBuildingActor>(
		ATerraDyneBuildingActor::StaticClass(),
		FTransform(FVector(0.0f, 0.0f, 500.0f)));
	TestNotNull(TEXT("Building spawned"), Building);
	if (!Building)
	{
		return false;
	}

	Building->bFlattenTerrain = true;
	Building->PadTargetWorldZ = 500.0f;
	Building->bAutoPadHeight = false;
	Building->NotifyBuildingChanged();

	TestTrue(TEXT("Chunk has building pad stamped"), Chunk->HasBuildingPad());

	// Unregister building and verify pad clears
	Manager->UnregisterBuilding(Building);
	TestFalse(TEXT("Chunk building pad cleared on building unregister"), Chunk->HasBuildingPad());

	Building->Destroy();
	return true;
}

// Regression test for a real gap: SetReplicateMovement's FRepMovement (used for
// bReplicates+SetReplicateMovement(true), ATerraDyneBuildingActor's own replication mechanism) only
// carries Location and Rotation -- it has no Scale3D field at all. Without an explicit replicated
// property, a gizmo Scale-mode drag would resize a building only on whichever machine performed the
// drag; every other client/observer would keep rendering it at its old size forever, with no error
// or warning anywhere. Checks the authority-side half of the fix -- NotifyBuildingChanged (called
// from the same commit path ATerraDyneEditController::RequestMoveBuilding uses) keeps
// ReplicatedScale3D in sync with the real GetActorScale3D() -- the same way this project's other
// Multiplayer tests verify replicated STATE rather than spinning up a second real network
// connection. OnRep_Scale3D (the remote-side apply, exercised once that property actually arrives on
// another machine) is protected like every other OnRep_* handler in this class and so isn't
// reachable from a test the way the existing OnRep_* handlers here already aren't either -- its own
// two-line body (apply + NotifyBuildingChanged, the same shape as OnRep_PadSettings) is confirmed by
// direct code reading instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneBuildingScaleReplicationTest,
	"TerraDyne.Multiplayer.BuildingScaleReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneBuildingScaleReplicationTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneBuildingActor* Building = World->SpawnActor<ATerraDyneBuildingActor>();
	TestNotNull(TEXT("Building spawned"), Building);
	if (!Building)
	{
		return false;
	}

	TestTrue(TEXT("ReplicatedScale3D starts matching the actor's spawn scale"), Building->ReplicatedScale3D.Equals(Building->GetActorScale3D(), 0.01));

	// A real gizmo Scale-mode commit ends by calling SetActorTransform (exercised end-to-end by
	// TerraDyne.Building.GizmoRotateAndScale) then NotifyBuildingChanged, exactly as
	// ATerraDyneEditController::RequestMoveBuilding does.
	Building->SetActorScale3D(FVector(3.0, 3.0, 3.0));
	TestFalse(TEXT("ReplicatedScale3D has not resynced yet, before NotifyBuildingChanged runs"), Building->ReplicatedScale3D.Equals(FVector(3.0), 0.01));
	Building->NotifyBuildingChanged();
	TestTrue(TEXT("NotifyBuildingChanged resyncs ReplicatedScale3D from the real actor scale"), Building->ReplicatedScale3D.Equals(FVector(3.0), 0.01));

	Building->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneGlobalNoiseSyncTest,
	"TerraDyne.Multiplayer.GlobalNoiseSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGlobalNoiseSyncTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull(TEXT("Manager spawned"), Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 2000.0f;

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull(TEXT("Chunk spawned"), Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint(0, 0);
	Chunk->WorldSize = 2000.0f;
	Chunk->ChunkSizeWorldUnits = 2000.0f;
	Chunk->Initialize(16, 2000.0f);
	Manager->RebuildChunkMap();

	// Record initial height sum
	float InitialHeightSum = 0.0f;
	for (float H : Chunk->HeightBuffer)
	{
		InitialHeightSum += H;
	}

	// Apply global noise
	Manager->ApplyGlobalNoise(500.0f, 0.002f, 42.0f);

	// Verify detail buffer and height buffer modified
	float PostNoiseSum = 0.0f;
	bool bHasNonZeroDetail = false;
	for (float D : Chunk->DetailBuffer)
	{
		if (!FMath::IsNearlyZero(D))
		{
			bHasNonZeroDetail = true;
			break;
		}
	}

	for (float H : Chunk->HeightBuffer)
	{
		PostNoiseSum += H;
	}

	TestTrue(TEXT("Detail buffer has non-zero values after noise application"), bHasNonZeroDetail);
	TestTrue(TEXT("Height buffer changed after noise application"), !FMath::IsNearlyEqual(InitialHeightSum, PostNoiseSum, 0.0001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneSaveAndLoadWorldWithBuildingsTest,
	"TerraDyne.Multiplayer.SaveAndLoadWorldWithBuildings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneSaveAndLoadWorldWithBuildingsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull(TEXT("Manager spawned"), Manager);
	if (!Manager)
	{
		return false;
	}

	UTerraDyneSubsystem* Subsystem = World->GetSubsystem<UTerraDyneSubsystem>();
	if (Subsystem)
	{
		Subsystem->RegisterManager(Manager);
	}

	// Spawn chunk
	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull(TEXT("Chunk spawned"), Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint(0, 0);
	Chunk->WorldSize = 5000.0f;
	Manager->RegisterChunk(Chunk);
	Manager->RebuildChunkMap();

	// Spawn building
	ATerraDyneBuildingActor* Building = World->SpawnActor<ATerraDyneBuildingActor>(
		ATerraDyneBuildingActor::StaticClass(),
		FTransform(FVector(100.0f, 200.0f, 300.0f)));
	TestNotNull(TEXT("Building spawned"), Building);
	if (!Building)
	{
		return false;
	}

	Building->SetFloorCount(5);
	Manager->RegisterBuilding(Building);

	// Save world to memory
	UTerraDyneSaveGame* SaveInst = Manager->BuildSaveGameInstance();
	TestNotNull(TEXT("Save game instance built"), SaveInst);
	if (!SaveInst)
	{
		return false;
	}

	TestTrue(TEXT("Save contains building data"), SaveInst->Buildings.Num() == 1);
	TestEqual(TEXT("Saved building floor count is 5"), SaveInst->Buildings[0].FloorCount, 5);

	// Test loading back
	Manager->ApplyLoadedSaveGame(SaveInst, TEXT("TestMemorySave"));

	// Find restored building
	TArray<AActor*> FoundBuildings;
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneBuildingActor::StaticClass(), FoundBuildings);
	TestEqual(TEXT("One building restored"), FoundBuildings.Num(), 1);

	if (FoundBuildings.Num() > 0)
	{
		ATerraDyneBuildingActor* RestoredBuilding = Cast<ATerraDyneBuildingActor>(FoundBuildings[0]);
		TestNotNull(TEXT("Restored building is valid"), RestoredBuilding);
		if (RestoredBuilding)
		{
			TestEqual(TEXT("Restored building floor count is 5"), RestoredBuilding->FloorCount, 5);
		}
	}

	return true;
}

#include "World/TerraDyneOrchestrator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneNoDuplicateChunksOnBeginPlayTest,
	"TerraDyne.Multiplayer.NoDuplicateChunksOnBeginPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneNoDuplicateChunksOnBeginPlayTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull(TEXT("Manager exists"), Manager);
	if (!Manager)
	{
		return false;
	}

	ATerraDyneOrchestrator* Orchestrator = World->SpawnActor<ATerraDyneOrchestrator>();
	TestNotNull(TEXT("Orchestrator exists"), Orchestrator);
	if (!Orchestrator)
	{
		return false;
	}

	Orchestrator->RestartShowcase();

	// Check for any duplicate chunks across all spawned actors
	TArray<AActor*> AllChunks;
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneChunk::StaticClass(), AllChunks);

	TMap<FIntPoint, int32> CoordCounts;
	for (AActor* Actor : AllChunks)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			CoordCounts.FindOrAdd(Chunk->GridCoordinate, 0)++;
		}
	}

	for (const auto& Pair : CoordCounts)
	{
		TestEqual(FString::Printf(TEXT("Coordinate [%d,%d] has exactly 1 chunk"), Pair.Key.X, Pair.Key.Y), Pair.Value, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneEditAuthorityTest,
	"TerraDyne.Multiplayer.ServerRejectsUnauthorizedEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Locks in the permission model for Server RPCs.
 *
 * Every Server_* body on ATerraDyneEditController is gated on HasEditAuthority(). This test pins
 * the two things that gate has to get right, because both were wrong before it existed: the flag
 * must actually deny, and IsInstructor() must not be mistaken for the server-side check.
 */
bool FTerraDyneEditAuthorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	ATerraDyneEditController* Controller = World->SpawnActor<ATerraDyneEditController>();
	TestNotNull(TEXT("Controller spawned"), Controller);
	if (!Controller)
	{
		return false;
	}

	// A controller with no Player is not a local controller — which is exactly the state a remote
	// client's controller is in on the server, and the case the guard exists to judge.
	const bool bIsLocal = Controller->IsLocalController();
	TestFalse(TEXT("Test controller stands in for a remote client (not local)"), bIsLocal);
	if (bIsLocal)
	{
		return false;
	}

	Controller->bCanEditTerrain = false;
	TestFalse(
		TEXT("Server denies a controller that was never granted edit rights"),
		Controller->HasEditAuthority());

	Controller->bCanEditTerrain = true;
	TestTrue(
		TEXT("Server allows the same controller once bCanEditTerrain is granted"),
		Controller->HasEditAuthority());

	// The reason the guard is not IsInstructor(): that predicate requires IsLocalController(), so
	// on the server it reads false for every remote client no matter what rights they hold. Used as
	// an RPC guard it would deny everyone while looking like a permission check.
	TestFalse(
		TEXT("IsInstructor stays false for a remote controller even with full edit rights"),
		Controller->IsInstructor());

	return true;
}

#endif

