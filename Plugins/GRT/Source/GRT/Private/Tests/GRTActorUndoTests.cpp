// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTEditController.h"
#include "Core/GRTAssetLibrarySubsystem.h"
#include "Building/GRTBuildingActor.h"
#include "Building/GRTPlacedModelActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

namespace
{
	int32 CountActorsOfClass(UWorld* World, UClass* Class)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, Class, Found);
		return Found.Num();
	}
}

// -----------------------------------------------------------------------------
// Test 1: Placing a building is undoable and redoable.
//
// Neither RequestPlaceBuilding nor RequestDeleteBuilding pushed anything onto any undo history
// before this feature -- a hand-placed building, once placed or deleted, could never be taken back
// with Ctrl+Z, unlike every terrain sculpt and every road edit. This is the straight-line case:
// place once, undo once, redo once.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTActorUndoPlaceBuildingTest, "GRT.ActorUndo.PlaceBuildingUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTActorUndoPlaceBuildingTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	TestNotNull(TEXT("Controller spawned"), Controller);
	if (!Controller) return false;

	TestEqual(TEXT("No buildings before placement"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(FVector(500, 500, 0)), 4);
	TestEqual(TEXT("One building exists after placement"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 1);

	TestTrue(TEXT("Undo reports it had something to take back"), Controller->TryActorUndo());
	TestEqual(TEXT("Undoing the placement removes the building"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	TestTrue(TEXT("Redo reports it had something to restore"), Controller->TryActorRedo());
	TArray<AActor*> Restored;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTBuildingActor::StaticClass(), Restored);
	TestEqual(TEXT("Redoing the placement brings exactly one building back"), Restored.Num(), 1);
	if (Restored.Num() == 1)
	{
		AGRTBuildingActor* Building = Cast<AGRTBuildingActor>(Restored[0]);
		TestNotNull(TEXT("Restored actor is a building"), Building);
		if (Building)
		{
			TestEqual(TEXT("Redo restores the floor count it was placed with"), Building->FloorCount, 4);
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Test 2: Deleting a building is undoable and redoable, restoring its real data.
//
// This is the case that actually exercises ToData()/ApplyData() as a snapshot: undo of a delete
// must bring back the SAME footprint and floor count, not a fresh default building.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTActorUndoDeleteBuildingTest, "GRT.ActorUndo.DeleteBuildingUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTActorUndoDeleteBuildingTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGRTBuildingActor* Building = World->SpawnActor<AGRTBuildingActor>(
		AGRTBuildingActor::StaticClass(), FTransform(FVector(1200, -400, 0)), SpawnParams);
	TestNotNull(TEXT("Building spawned directly for this test"), Building);
	if (!Building) return false;

	Building->SetFloorCount(7);
	const TArray<FVector> DistinctiveFootprint = {
		FVector(-150, -100, 0), FVector(150, -100, 0), FVector(150, 100, 0), FVector(-150, 100, 0)
	};
	Building->SetSplinePoints(DistinctiveFootprint);
	const FGuid OriginalId = Building->BuildingId;

	Controller->RequestDeleteBuilding(Building);
	TestEqual(TEXT("Building is gone after delete"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	TestTrue(TEXT("Undo restores it"), Controller->TryActorUndo());
	TArray<AActor*> Restored;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTBuildingActor::StaticClass(), Restored);
	TestEqual(TEXT("Exactly one building restored"), Restored.Num(), 1);
	if (Restored.Num() == 1)
	{
		AGRTBuildingActor* RestoredBuilding = Cast<AGRTBuildingActor>(Restored[0]);
		if (TestNotNull(TEXT("Restored actor is a building"), RestoredBuilding))
		{
			TestEqual(TEXT("Restored floor count matches what was deleted"), RestoredBuilding->FloorCount, 7);
			TestEqual(TEXT("Restored BuildingId matches -- other systems may reference it by id"), RestoredBuilding->BuildingId, OriginalId);

			const TArray<FVector> RestoredFootprint = RestoredBuilding->GatherSplinePointsLocal();
			TestEqual(TEXT("Restored footprint has the same point count"), RestoredFootprint.Num(), DistinctiveFootprint.Num());
			bool bAllMatch = RestoredFootprint.Num() == DistinctiveFootprint.Num();
			for (int32 i = 0; bAllMatch && i < RestoredFootprint.Num(); ++i)
			{
				bAllMatch &= RestoredFootprint[i].Equals(DistinctiveFootprint[i], 1.0);
			}
			TestTrue(TEXT("Restored footprint points match the deleted building's, not a default"), bAllMatch);
		}
	}

	TestTrue(TEXT("Redo deletes it again"), Controller->TryActorRedo());
	TestEqual(TEXT("Building gone again after redo"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	return true;
}

// -----------------------------------------------------------------------------
// Test 3: Placing and deleting a placed 3D model are both undoable/redoable.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTActorUndoModelTest, "GRT.ActorUndo.ModelPlaceAndDeleteUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTActorUndoModelTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	UGRTAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UGRTAssetLibrarySubsystem>();
	if (!Controller || !AssetLib) return false;
	AssetLib->ClearLibrary();

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("ActorUndoTri.obj"));
	FFileHelper::SaveStringToFile(FString(TEXT("v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n")), *ObjPath);

	FString Error;
	const FGuid AssetId = AssetLib->ImportModelFromFile(ObjPath, Error);
	TestTrue(TEXT("Model asset imports for the fixture"), AssetId.IsValid());
	if (!AssetId.IsValid()) return false;

	// --- Placement ---
	Controller->RequestPlaceModel(AssetId, FTransform(FVector(300, 300, 0)));
	TestEqual(TEXT("One model placed"), CountActorsOfClass(World, AGRTPlacedModelActor::StaticClass()), 1);

	TestTrue(TEXT("Undo removes the placed model"), Controller->TryActorUndo());
	TestEqual(TEXT("Model gone after undo"), CountActorsOfClass(World, AGRTPlacedModelActor::StaticClass()), 0);

	TestTrue(TEXT("Redo restores the placed model"), Controller->TryActorRedo());
	TestEqual(TEXT("Model back after redo"), CountActorsOfClass(World, AGRTPlacedModelActor::StaticClass()), 1);

	// --- Deletion of the actor now in the world ---
	TArray<AActor*> Models;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTPlacedModelActor::StaticClass(), Models);
	TestEqual(TEXT("Exactly one model present before delete"), Models.Num(), 1);
	if (Models.Num() != 1) return false;

	AGRTPlacedModelActor* ModelActor = Cast<AGRTPlacedModelActor>(Models[0]);
	ModelActor->CustomLabel = TEXT("Distinctive Label");
	Controller->RequestDeleteModel(ModelActor);
	TestEqual(TEXT("Model gone after delete"), CountActorsOfClass(World, AGRTPlacedModelActor::StaticClass()), 0);

	TestTrue(TEXT("Undo restores the deleted model"), Controller->TryActorUndo());
	TArray<AActor*> RestoredModels;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTPlacedModelActor::StaticClass(), RestoredModels);
	TestEqual(TEXT("Exactly one model restored"), RestoredModels.Num(), 1);
	if (RestoredModels.Num() == 1)
	{
		AGRTPlacedModelActor* Restored = Cast<AGRTPlacedModelActor>(RestoredModels[0]);
		if (TestNotNull(TEXT("Restored actor is a model"), Restored))
		{
			TestEqual(TEXT("Restored model keeps its custom label"), Restored->CustomLabel, FString(TEXT("Distinctive Label")));
			TestEqual(TEXT("Restored model keeps its asset id"), Restored->AssetId, AssetId);
		}
	}

	TestTrue(TEXT("Redo deletes it again"), Controller->TryActorRedo());
	TestEqual(TEXT("Model gone again after redo"), CountActorsOfClass(World, AGRTPlacedModelActor::StaticClass()), 0);

	return true;
}

// -----------------------------------------------------------------------------
// Test 4: A fresh action clears the redo stack -- matches CommitStroke's identical handling of
// terrain's RedoStacks, so building/model undo doesn't quietly behave differently.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTActorUndoRedoClearedTest, "GRT.ActorUndo.RedoClearedByNewAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTActorUndoRedoClearedTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(FVector(0, 0, 0)), 2);
	Controller->TryActorUndo(); // now there is one redo-able action pending

	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(FVector(2000, 0, 0)), 3);
	TestEqual(TEXT("The new placement exists"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 1);

	TestFalse(TEXT("Redo has nothing left -- the new action invalidated it"), Controller->TryActorRedo());
	TestEqual(TEXT("Redo did not resurrect the first building"), CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 1);

	return true;
}

// -----------------------------------------------------------------------------
// Test 5: An OSM import does not spawn everything in one call -- it queues, and drains a bounded
// number of features per pump. Proves the "does not hitch the game thread" property directly:
// immediately after RequestImportOSM returns, most queued buildings must NOT exist yet.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTOSMAsyncDrainTest, "GRT.OSM.AsyncSpawnQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTOSMAsyncDrainTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	// 12 free-standing 4-corner buildings, well clear of MaxOSMFeaturesPerTick (8), so a single
	// drain call is provably partial.
	constexpr int32 BuildingCount = 12;
	FString Xml = TEXT("<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6'>\n");
	int32 NodeId = 1;
	for (int32 i = 0; i < BuildingCount; ++i)
	{
		const double BaseLat = 51.5000 + i * 0.001;
		const double BaseLon = -0.1300;
		const int32 N0 = NodeId++, N1 = NodeId++, N2 = NodeId++, N3 = NodeId++;
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N0, BaseLat, BaseLon);
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N1, BaseLat, BaseLon + 0.0005);
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N2, BaseLat + 0.0005, BaseLon + 0.0005);
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N3, BaseLat + 0.0005, BaseLon);
		Xml += FString::Printf(
			TEXT("<way id='%d'><nd ref='%d'/><nd ref='%d'/><nd ref='%d'/><nd ref='%d'/><nd ref='%d'/><tag k='building' v='yes'/></way>\n"),
			1000 + i, N0, N1, N2, N3, N0);
	}
	Xml += TEXT("</osm>\n");

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString OsmPath = FPaths::Combine(TestDir, TEXT("AsyncDrainFixture.osm"));
	FFileHelper::SaveStringToFile(Xml, *OsmPath);

	Controller->RequestImportOSM(OsmPath, /*bImportBuildings*/ true);

	int32 Done = 0, Total = 0;
	Controller->GetOSMImportProgress(Done, Total);
	AddInfo(FString::Printf(TEXT("Immediately after RequestImportOSM: Done=%d Total=%d InProgress=%d Buildings=%d"),
		Done, Total, Controller->IsOSMImportInProgress() ? 1 : 0, CountActorsOfClass(World, AGRTBuildingActor::StaticClass())));

	TestEqual(TEXT("All 12 buildings were resolved and queued"), Total, BuildingCount);
	TestTrue(TEXT("The import is in progress immediately after the request -- it did not finish synchronously"),
		Controller->IsOSMImportInProgress());
	TestEqual(TEXT("No buildings exist yet -- only the CPU-only resolve phase ran synchronously"),
		CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	// One drain call must be bounded, not exhaustive.
	Controller->ProcessOSMSpawnQueue();
	const int32 AfterOneDrain = CountActorsOfClass(World, AGRTBuildingActor::StaticClass());
	TestTrue(TEXT("A single drain call spawns a bounded batch, not everything at once"),
		AfterOneDrain > 0 && AfterOneDrain < BuildingCount);

	// Keep pumping until it finishes (bounded loop so a real bug fails the test instead of hanging it).
	int32 SafetyPumps = 0;
	while (Controller->IsOSMImportInProgress() && SafetyPumps < 100)
	{
		Controller->ProcessOSMSpawnQueue();
		++SafetyPumps;
	}

	TestFalse(TEXT("Import eventually finishes"), Controller->IsOSMImportInProgress());
	TestEqual(TEXT("All 12 buildings exist once the queue is fully drained"),
		CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), BuildingCount);

	Controller->GetOSMImportProgress(Done, Total);
	TestEqual(TEXT("Progress reports fully done"), Done, Total);

	const FGRTOSMImportSummary Summary = Controller->GetLastOSMImportSummary();
	TestTrue(TEXT("Final summary reports success"), Summary.bSuccess);
	TestEqual(TEXT("Final summary's building count matches"), Summary.BuildingsCreated, BuildingCount);

	return true;
}

// -----------------------------------------------------------------------------
// Test 6: One Ctrl+Z reverses an entire OSM import's batch of spawns, not just the last feature.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTOSMBatchUndoTest, "GRT.OSM.BatchUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTOSMBatchUndoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	constexpr int32 BuildingCount = 5;
	FString Xml = TEXT("<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6'>\n");
	int32 NodeId = 1;
	for (int32 i = 0; i < BuildingCount; ++i)
	{
		const double BaseLat = 51.5100 + i * 0.001;
		const double BaseLon = -0.1200;
		const int32 N0 = NodeId++, N1 = NodeId++, N2 = NodeId++, N3 = NodeId++;
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N0, BaseLat, BaseLon);
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N1, BaseLat, BaseLon + 0.0005);
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N2, BaseLat + 0.0005, BaseLon + 0.0005);
		Xml += FString::Printf(TEXT("<node id='%d' lat='%f' lon='%f'/>\n"), N3, BaseLat + 0.0005, BaseLon);
		Xml += FString::Printf(
			TEXT("<way id='%d'><nd ref='%d'/><nd ref='%d'/><nd ref='%d'/><nd ref='%d'/><nd ref='%d'/><tag k='building' v='yes'/></way>\n"),
			2000 + i, N0, N1, N2, N3, N0);
	}
	Xml += TEXT("</osm>\n");

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString OsmPath = FPaths::Combine(TestDir, TEXT("BatchUndoFixture.osm"));
	FFileHelper::SaveStringToFile(Xml, *OsmPath);

	Controller->RequestImportOSM(OsmPath, true);

	int32 SafetyPumps = 0;
	while (Controller->IsOSMImportInProgress() && SafetyPumps < 100)
	{
		Controller->ProcessOSMSpawnQueue();
		++SafetyPumps;
	}
	TestEqual(TEXT("All buildings from the batch exist before undo"),
		CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), BuildingCount);

	TestTrue(TEXT("A single undo call succeeds"), Controller->TryActorUndo());
	TestEqual(TEXT("One Ctrl+Z removes the WHOLE batch, not just one building"),
		CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	TestTrue(TEXT("Redo brings the whole batch back"), Controller->TryActorRedo());
	TestEqual(TEXT("All buildings restored by one redo"),
		CountActorsOfClass(World, AGRTBuildingActor::StaticClass()), BuildingCount);

	return true;
}

#endif // WITH_EDITOR
