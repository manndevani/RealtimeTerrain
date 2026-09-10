// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// Regression tests for the 2026-09-10 code-review fixes. Each test checks the behaviour a user
// or another system actually depends on (where the actor ends up, what the save contains, which
// texture the material samples), not merely that the call returns.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTEditController.h"
#include "Core/GRTManager.h"
#include "Core/GRTSubsystem.h"
#include "Core/GRTSaveGame.h"
#include "Core/GRTAssetLibrarySubsystem.h"
#include "Building/GRTBuildingActor.h"
#include "Building/GRTPlacedModelActor.h"
#include "World/GRTChunk.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

namespace
{
	int32 CountOfClass(UWorld* World, UClass* Class)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, Class, Found);
		return Found.Num();
	}

	AGRTBuildingActor* OnlyBuilding(UWorld* World)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, AGRTBuildingActor::StaticClass(), Found);
		return Found.Num() == 1 ? Cast<AGRTBuildingActor>(Found[0]) : nullptr;
	}

	bool SameXY(const FVector& A, const FVector& B)
	{
		return FMath::IsNearlyEqual(A.X, B.X, 1.0) && FMath::IsNearlyEqual(A.Y, B.Y, 1.0);
	}

	/** Manager registered with the subsystem, plus one 5000uu chunk at (0,0) -- the BuildingPadSync fixture. */
	AGRTChunk* SpawnManagerAndChunk(UWorld* World, AGRTManager*& OutManager)
	{
		OutManager = World->SpawnActor<AGRTManager>();
		if (!OutManager)
		{
			return nullptr;
		}
		OutManager->GlobalChunkSize = 5000.0f;
		if (UGRTSubsystem* Subsystem = World->GetSubsystem<UGRTSubsystem>())
		{
			Subsystem->RegisterManager(OutManager);
		}

		AGRTChunk* Chunk = World->SpawnActor<AGRTChunk>();
		if (Chunk)
		{
			Chunk->GridCoordinate = FIntPoint(0, 0);
			Chunk->WorldSize = 5000.0f;
			OutManager->RegisterChunk(Chunk);
			OutManager->RebuildChunkMap();
		}
		return Chunk;
	}
}

// -----------------------------------------------------------------------------
// Moving a building is undoable, and undo moves it back instead of deleting it.
// Before the fix only place/delete were recorded, so Ctrl+Z after a move undid the placement.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTUndoMoveBuildingTest, "GRT.ActorUndo.MoveBuildingUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTUndoMoveBuildingTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	const FVector StartLoc(500, 500, 0);
	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(StartLoc), 3);
	AGRTBuildingActor* Building = OnlyBuilding(World);
	if (!TestNotNull(TEXT("Building placed"), Building)) return false;

	const FVector MovedLoc(4000, -2000, 0);
	Controller->RequestMoveBuilding(Building, FTransform(FRotator(0, 30, 0), MovedLoc));
	TestTrue(TEXT("Building moved"), SameXY(Building->GetActorLocation(), MovedLoc));

	TestTrue(TEXT("Undo has an entry"), Controller->TryActorUndo());
	AGRTBuildingActor* AfterUndo = OnlyBuilding(World);
	if (!TestNotNull(TEXT("Undo of a move keeps the building (it used to delete it)"), AfterUndo)) return false;
	TestTrue(TEXT("Undo returns it to where it was placed"), SameXY(AfterUndo->GetActorLocation(), StartLoc));
	TestTrue(TEXT("Undo restores its rotation"), AfterUndo->GetActorRotation().Equals(FRotator::ZeroRotator, 0.1f));

	TestTrue(TEXT("Redo has an entry"), Controller->TryActorRedo());
	TestTrue(TEXT("Redo moves it again"), SameXY(AfterUndo->GetActorLocation(), MovedLoc));

	TestTrue(TEXT("Second undo reaches the move"), Controller->TryActorUndo());
	TestTrue(TEXT("Third undo reaches the placement"), Controller->TryActorUndo());
	TestEqual(TEXT("Only undoing the placement removes the building"), CountOfClass(World, AGRTBuildingActor::StaticClass()), 0);
	return true;
}

// -----------------------------------------------------------------------------
// A move recorded before a delete still finds its building after the delete is undone. The undo
// re-creates a new actor, so the older entry must be retargeted at it.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTUndoModifySurvivesRespawnTest, "GRT.ActorUndo.ModifyEntrySurvivesRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTUndoModifySurvivesRespawnTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	const FVector StartLoc(0, 0, 0);
	const FVector MovedLoc(3000, 3000, 0);
	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(StartLoc), 2);
	AGRTBuildingActor* Building = OnlyBuilding(World);
	if (!Building) return false;

	Controller->RequestMoveBuilding(Building, FTransform(MovedLoc));
	Controller->RequestDeleteBuilding(Building);
	TestEqual(TEXT("Deleted"), CountOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	TestTrue(TEXT("Undo delete"), Controller->TryActorUndo());
	AGRTBuildingActor* Restored = OnlyBuilding(World);
	if (!TestNotNull(TEXT("Delete undone"), Restored)) return false;
	TestTrue(TEXT("Restored where it was deleted"), SameXY(Restored->GetActorLocation(), MovedLoc));

	TestTrue(TEXT("Undo move"), Controller->TryActorUndo());
	TestTrue(TEXT("The older move entry drove the re-created building back to its start"),
		SameXY(Restored->GetActorLocation(), StartLoc));
	return true;
}

// -----------------------------------------------------------------------------
// Floor-count edits and no-op moves.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTUndoFloorsAndNoOpTest, "GRT.ActorUndo.FloorCountAndNoOpMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTUndoFloorsAndNoOpTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(FVector(100, 100, 0)), 2);
	AGRTBuildingActor* Building = OnlyBuilding(World);
	if (!Building) return false;

	Controller->RequestSetBuildingFloorCount(Building, 6);
	TestEqual(TEXT("Floors set"), Building->FloorCount, 6);

	// A release without movement must not bury real history under an entry that does nothing.
	Controller->RequestMoveBuilding(Building, Building->GetActorTransform());

	TestTrue(TEXT("Undo"), Controller->TryActorUndo());
	TestEqual(TEXT("First undo reverts the floor count -- the no-op move recorded nothing"), Building->FloorCount, 2);

	TestTrue(TEXT("Undo placement"), Controller->TryActorUndo());
	TestEqual(TEXT("Then the placement"), CountOfClass(World, AGRTBuildingActor::StaticClass()), 0);
	return true;
}

// -----------------------------------------------------------------------------
// RequestUndo (what Ctrl+Z calls, locally and on the server for a client) runs the actor history.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTRequestUndoChainTest, "GRT.ActorUndo.RequestUndoRunsActorHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTRequestUndoChainTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Controller) return false;

	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(FVector::ZeroVector), 2);
	TestEqual(TEXT("Placed"), CountOfClass(World, AGRTBuildingActor::StaticClass()), 1);

	Controller->RequestUndo();
	TestEqual(TEXT("RequestUndo undid the placement"), CountOfClass(World, AGRTBuildingActor::StaticClass()), 0);

	Controller->RequestRedo();
	TestEqual(TEXT("RequestRedo restored it"), CountOfClass(World, AGRTBuildingActor::StaticClass()), 1);
	return true;
}

// -----------------------------------------------------------------------------
// Moving a placed model is undoable, scale included.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTUndoMoveModelTest, "GRT.ActorUndo.MoveModelUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTUndoMoveModelTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	UGRTAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UGRTAssetLibrarySubsystem>();
	if (!Controller || !AssetLib) return false;
	AssetLib->ClearLibrary();

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("ReviewFixTri.obj"));
	FFileHelper::SaveStringToFile(FString(TEXT("v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n")), *ObjPath);

	FString Error;
	const FGuid AssetId = AssetLib->ImportModelFromFile(ObjPath, Error);
	if (!TestTrue(TEXT("Fixture imports"), AssetId.IsValid())) return false;

	const FTransform Start(FVector(300, 300, 0));
	Controller->RequestPlaceModel(AssetId, Start);
	TArray<AActor*> Models;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTPlacedModelActor::StaticClass(), Models);
	if (!TestEqual(TEXT("One model"), Models.Num(), 1)) return false;
	AGRTPlacedModelActor* Model = Cast<AGRTPlacedModelActor>(Models[0]);

	const FTransform Moved(FRotator(0, 45, 0), FVector(900, -600, 50), FVector(2.0));
	Controller->RequestMoveModel(Model, Moved);
	TestTrue(TEXT("Model moved"), Model->GetActorTransform().Equals(Moved, 0.01));

	TestTrue(TEXT("Undo"), Controller->TryActorUndo());
	TestEqual(TEXT("Undo of a model move keeps the model"), CountOfClass(World, AGRTPlacedModelActor::StaticClass()), 1);
	TestTrue(TEXT("Undo restores the full transform"), Model->GetActorTransform().Equals(Start, 0.01));
	TestTrue(TEXT("Undo restores the replicated scale too"), Model->ReplicatedScale3D.Equals(FVector::OneVector, 1e-4));

	TestTrue(TEXT("Redo"), Controller->TryActorRedo());
	TestTrue(TEXT("Redo reapplies the move"), Model->GetActorTransform().Equals(Moved, 0.01));
	return true;
}

// -----------------------------------------------------------------------------
// Loading a world clears building/model history, so undo cannot resurrect a pre-load delete.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTLoadClearsActorHistoryTest, "GRT.Persistence.LoadClearsActorHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTLoadClearsActorHistoryTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTManager* Manager = nullptr;
	SpawnManagerAndChunk(World, Manager);
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	if (!Manager || !Controller) return false;

	// An empty world to load later.
	UGRTSaveGame* EmptyWorld = Manager->BuildSaveGameInstance();
	if (!TestNotNull(TEXT("Save built"), EmptyWorld)) return false;

	Controller->RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(FVector(200, 200, 0)), 2);
	AGRTBuildingActor* Building = OnlyBuilding(World);
	if (!Building) return false;
	Controller->RequestDeleteBuilding(Building);

	Manager->ApplyLoadedSaveGame(EmptyWorld, TEXT("ReviewFixEmptyWorld"));

	TestFalse(TEXT("History from before the load is gone"), Controller->TryActorUndo());
	TestEqual(TEXT("Undo did not resurrect the deleted building into the loaded world"),
		CountOfClass(World, AGRTBuildingActor::StaticClass()), 0);
	return true;
}

// -----------------------------------------------------------------------------
// Scale, pad settings and footprint survive a save round trip, and the footprint reaches the
// replicated array clients read.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTBuildingDataRoundTripTest, "GRT.Persistence.BuildingDataRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTBuildingDataRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTBuildingActor* Source = World->SpawnActor<AGRTBuildingActor>(AGRTBuildingActor::StaticClass(), FTransform(FVector(1000, 0, 0)));
	if (!Source) return false;

	const TArray<FVector> Footprint = { FVector(-200, -100, 0), FVector(200, -100, 0), FVector(250, 150, 0), FVector(-200, 100, 0) };
	Source->SetSplinePoints(Footprint);
	Source->SetActorScale3D(FVector(2.0, 1.5, 1.0));
	Source->bFlattenTerrain = false;
	Source->bAutoPadHeight = false;
	Source->PadTargetWorldZ = 123.0f;
	Source->PadFalloff = 250.0f;

	const FGRTBuildingData Data = Source->ToData();
	TestTrue(TEXT("ToData captures scale"), Data.Scale.Equals(FVector(2.0, 1.5, 1.0), 1e-4));

	AGRTBuildingActor* Restored = AGRTBuildingActor::SpawnFromData(World, Data);
	if (!TestNotNull(TEXT("Spawned from data"), Restored)) return false;

	TestTrue(TEXT("Scale restored"), Restored->GetActorScale3D().Equals(FVector(2.0, 1.5, 1.0), 1e-4));
	TestTrue(TEXT("Replicated scale matches, so clients see it"), Restored->ReplicatedScale3D.Equals(FVector(2.0, 1.5, 1.0), 1e-4));
	TestFalse(TEXT("bFlattenTerrain restored"), Restored->bFlattenTerrain);
	TestFalse(TEXT("bAutoPadHeight restored"), Restored->bAutoPadHeight);
	TestEqual(TEXT("PadTargetWorldZ restored"), Restored->PadTargetWorldZ, 123.0f);
	TestEqual(TEXT("PadFalloff restored"), Restored->PadFalloff, 250.0f);

	bool bReplicatedMatches = Restored->ReplicatedSplinePoints.Num() == Footprint.Num();
	for (int32 i = 0; bReplicatedMatches && i < Footprint.Num(); ++i)
	{
		bReplicatedMatches &= Restored->ReplicatedSplinePoints[i].Equals(Footprint[i], 0.5);
	}
	TestTrue(TEXT("ReplicatedSplinePoints carry the restored footprint, not the default rectangle"), bReplicatedMatches);
	return true;
}

// -----------------------------------------------------------------------------
// Non-flattening buildings leave the terrain alone -- from spawn, and when flattening is turned
// off after a pad was stamped -- and are still found by save/load.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTNonFlatteningPadTest, "GRT.Building.NonFlatteningLeavesTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTNonFlatteningPadTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTManager* Manager = nullptr;
	AGRTChunk* Chunk = SpawnManagerAndChunk(World, Manager);
	if (!Manager || !Chunk) return false;

	FGRTBuildingData Data;
	Data.Origin = FVector(0, 0, 500);
	Data.SplinePoints = { FVector(-400, -400, 0), FVector(400, -400, 0), FVector(400, 400, 0), FVector(-400, 400, 0) };
	Data.bFlattenTerrain = false;
	Data.bAutoPadHeight = false;
	Data.PadTargetZ = 500.0f;

	AGRTBuildingActor* NonFlat = AGRTBuildingActor::SpawnFromData(World, Data);
	if (!NonFlat) return false;
	TestFalse(TEXT("A non-flattening building stamps no pad, even during its own spawn"), Chunk->HasBuildingPad());

	UGRTSaveGame* Save = Manager->BuildSaveGameInstance();
	TestTrue(TEXT("It is still registered, so it is saved"), Save && Save->Buildings.Num() == 1);
	TestTrue(TEXT("And saved as non-flattening"), Save && Save->Buildings.Num() == 1 && !Save->Buildings[0].bFlattenTerrain);
	NonFlat->Destroy();

	Data.bFlattenTerrain = true;
	AGRTBuildingActor* Flat = AGRTBuildingActor::SpawnFromData(World, Data);
	if (!Flat) return false;
	TestTrue(TEXT("A flattening building stamps its pad"), Chunk->HasBuildingPad());

	Flat->bFlattenTerrain = false;
	Flat->NotifyBuildingChanged();
	TestFalse(TEXT("Turning flattening off gives the ground back"), Chunk->HasBuildingPad());
	return true;
}

// -----------------------------------------------------------------------------
// The chunk eviction cache belongs to one world session.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTChunkCacheSessionTest, "GRT.Streaming.ChunkCacheIsSessionScoped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTChunkCacheSessionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTManager* SessionA = World->SpawnActor<AGRTManager>();
	AGRTManager* SessionB = World->SpawnActor<AGRTManager>();
	if (!SessionA || !SessionB) return false;

	const FIntPoint Coord(7, -3);
	FGRTChunkData Data;
	Data.Coordinate = Coord;
	Data.Resolution = 2;
	Data.ZScale = 1.0f;

	SessionA->SaveChunkToCache(Coord, Data);
	TestTrue(TEXT("Session A tracks what it cached"), SessionA->IsChunkCached(Coord));
	FGRTChunkData ReadBack;
	TestTrue(TEXT("Session A reads its own cached chunk"), SessionA->LoadChunkFromCache(Coord, ReadBack));
	TestEqual(TEXT("Cached coordinate round-trips"), ReadBack.Coordinate, Coord);

	FGRTChunkData Foreign;
	TestFalse(TEXT("Another session does not see A's cached chunk"), SessionB->LoadChunkFromCache(Coord, Foreign));
	TestFalse(TEXT("And does not think it has it"), SessionB->IsChunkCached(Coord));

	UGRTSaveGame* Save = SessionA->BuildSaveGameInstance();
	TestTrue(TEXT("An evicted chunk is folded into the save"), Save && Save->Chunks.Contains(Coord));

	SessionA->ResetChunkCacheSession();
	FGRTChunkData AfterReset;
	TestFalse(TEXT("After a world reset the old cache is gone"), SessionA->LoadChunkFromCache(Coord, AfterReset));
	TestFalse(TEXT("And no longer tracked"), SessionA->IsChunkCached(Coord));

	SessionB->ResetChunkCacheSession();
	return true;
}

// -----------------------------------------------------------------------------
// Imported textures actually reach the material. BasicShapeMaterial has no texture parameter, so
// before the fix every texture assignment was silently dropped.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTModelTextureBindingTest, "GRT.Model.TextureReachesMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTModelTextureBindingTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	AGRTPlacedModelActor* Model = World->SpawnActor<AGRTPlacedModelActor>();
	if (!Model) return false;
	if (!TestNotNull(TEXT("M_GRT_ImportedModel resolved as the base material"), Model->ImportedModelBaseMaterial.Get())) return false;

	UTexture2D* Texture = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	if (!Texture) return false;

	FGRTImportedModelAsset Asset;
	Asset.AssetId = FGuid::NewGuid();
	Asset.AssetName = TEXT("TextureBindingFixture");
	FGRTImportedMaterial Textured;
	Textured.DiffuseColor = FLinearColor(0.5f, 0.25f, 1.0f);
	Textured.DiffuseTexture = Texture;
	Asset.Materials.Add(Textured);

	Model->SetModelAsset(Asset);

	UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(Model->DynamicMeshComponent->GetMaterial(0));
	if (!TestNotNull(TEXT("Slot 0 has a dynamic instance"), DMI)) return false;
	TestEqual(TEXT("Instanced from the imported-model material"), DMI->Parent.Get(), Model->ImportedModelBaseMaterial.Get());

	UTexture* Bound = nullptr;
	TestTrue(TEXT("BaseColorTexture is a real parameter of the base"),
		DMI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseColorTexture")), Bound));
	TestEqual(TEXT("The imported texture is what the material samples"), Bound, static_cast<UTexture*>(Texture));

	FLinearColor Color;
	TestTrue(TEXT("BaseColor is a real parameter of the base"),
		DMI->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), Color));
	TestTrue(TEXT("The diffuse colour is applied"), Color.Equals(FLinearColor(0.5f, 0.25f, 1.0f), 1e-3f));
	return true;
}

// -----------------------------------------------------------------------------
// Outliner visibility goes through the server and replicates, collision included.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTModelVisibilityTest, "GRT.Model.VisibilityReplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTModelVisibilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	AGRTEditController* Controller = World->SpawnActor<AGRTEditController>();
	AGRTPlacedModelActor* Model = World->SpawnActor<AGRTPlacedModelActor>();
	if (!Controller || !Model) return false;

	const FProperty* VisibleProp = FindFProperty<FProperty>(AGRTPlacedModelActor::StaticClass(), TEXT("bIsVisibleInScene"));
	TestTrue(TEXT("bIsVisibleInScene is a replicated property"), VisibleProp && VisibleProp->HasAnyPropertyFlags(CPF_Net));

	Controller->RequestSetActorVisible(Model, false);
	TestFalse(TEXT("Hidden flag set"), Model->bIsVisibleInScene);
	TestTrue(TEXT("Actor hidden"), Model->IsHidden());
	TestFalse(TEXT("Collision off while hidden"), Model->GetActorEnableCollision());

	Controller->RequestSetActorVisible(Model, true);
	TestTrue(TEXT("Shown again"), Model->bIsVisibleInScene && !Model->IsHidden() && Model->GetActorEnableCollision());
	return true;
}

#endif // WITH_EDITOR
