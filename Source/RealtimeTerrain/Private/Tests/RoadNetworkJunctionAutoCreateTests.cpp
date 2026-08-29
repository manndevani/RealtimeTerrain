// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
//
// Regression coverage for a real gap: growing a road (by any of the three ways this project can
// create one) never triggered junction crossing-detection, so a road drawn across another just
// visually overlapped it -- no junction, no lane cut, no gore fill. Dragging an EXISTING point
// already called ARealtimeRoadSplineActor::RefreshAffectedJunctions correctly; none of
// URealtimeRoadBuilderComponent::HandleWorldClick's extend branch, SpawnRoadAuthoritative (the
// draft-then-commit path), or ARealtimeRoadEditController::CreateRoadFromOSM did. Before this fix,
// none of the three call paths below produced a junction at the crossing at all -- these tests
// exercise the real production entry points, not RefreshAffectedJunctions in isolation, so a
// regression that removes the call again fails exactly the way the original bug did.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RealtimeRoadSplineActor.h"
#include "RealtimeRoadJunctionActor.h"
#include "RealtimeRoadEditController.h"
#include "RealtimeRoadBuilderComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

namespace
{
	/** Spawns a road directly (bypassing the builder component) so a test can set up "the other,
	 *  already-existing road" without depending on the very paths under test. */
	ARealtimeRoadSplineActor* SpawnStraightRoad(UWorld* World, const FVector& A, const FVector& B)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>(
			ARealtimeRoadSplineActor::StaticClass(), A, FRotator::ZeroRotator, SpawnParams);
		if (Road)
		{
			Road->RoadType = ERoadType::PavedRoad;
			Road->bRebaseTerrain = false;
			Road->bConformToTerrainInRealtime = false;
			Road->SetPointsRealtime({ A, B });
			Road->RebuildRoad();
		}
		return Road;
	}

	int32 CountJunctions(UWorld* World)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, ARealtimeRoadJunctionActor::StaticClass(), Found);
		return Found.Num();
	}
}

// -----------------------------------------------------------------------------
// Path 1: click-to-extend, the path AutoPointSpacing actually feeds. Two clicks draw a road along
// X; two more draw a second road along Y, crossing it at the origin. Neither click snaps onto a
// junction leg (there isn't one yet), so this can only pass if RefreshAffectedJunctions is being
// called from the crossing-detection side, not the leg-snap side.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkClickExtendCreatesJunctionTest, "TerraDyne.RoadNetwork.ClickExtendCreatesJunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadNetworkClickExtendCreatesJunctionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	ARealtimeRoadEditController* Controller = World->SpawnActor<ARealtimeRoadEditController>();
	TestNotNull(TEXT("Controller spawned"), Controller);
	if (!Controller || !Controller->RoadBuilder) return false;

	URealtimeRoadBuilderComponent* Builder = Controller->RoadBuilder;
	Builder->bSnapToTerrain = false; // no TerraDyne terrain in this world to project onto

	TestEqual(TEXT("No junctions before drawing anything"), CountJunctions(World), 0);

	// Road A: two clicks along X, through the origin.
	Builder->HandleWorldClick(FVector(-1000.0, 0.0, 0.0));
	Builder->HandleWorldClick(FVector(1000.0, 0.0, 0.0));
	Builder->ClearSelection();

	TArray<AActor*> RoadsAfterA;
	UGameplayStatics::GetAllActorsOfClass(World, ARealtimeRoadSplineActor::StaticClass(), RoadsAfterA);
	TestEqual(TEXT("Road A exists as one actor"), RoadsAfterA.Num(), 1);
	TestEqual(TEXT("Drawing a single road crosses nothing yet"), CountJunctions(World), 0);

	// Road B: two clicks along Y, through the origin -- crosses Road A there.
	Builder->HandleWorldClick(FVector(0.0, -1000.0, 0.0));
	Builder->HandleWorldClick(FVector(0.0, 1000.0, 0.0));

	TArray<AActor*> RoadsAfterB;
	UGameplayStatics::GetAllActorsOfClass(World, ARealtimeRoadSplineActor::StaticClass(), RoadsAfterB);
	TestEqual(TEXT("Both roads exist as separate actors"), RoadsAfterB.Num(), 2);

	const int32 JunctionCount = CountJunctions(World);
	AddInfo(FString::Printf(TEXT("Junctions after drawing a crossing road via clicks: %d"), JunctionCount));
	TestTrue(TEXT("A junction was auto-created where the two clicked roads cross"), JunctionCount >= 1);

	if (JunctionCount >= 1)
	{
		TArray<AActor*> Junctions;
		UGameplayStatics::GetAllActorsOfClass(World, ARealtimeRoadJunctionActor::StaticClass(), Junctions);
		ARealtimeRoadJunctionActor* Junction = Cast<ARealtimeRoadJunctionActor>(Junctions[0]);
		if (Junction)
		{
			TestTrue(TEXT("The junction sits near the true crossing point (the origin)"),
				FVector::Dist2D(Junction->GetActorLocation(), FVector::ZeroVector) < 200.0);
			TestTrue(TEXT("It was auto-generated by crossing detection, not hand-placed"), Junction->bAutoGenerated);
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Path 2: draft-then-commit (BeginRoadDraft/CommitPoint/FinishRoadDraft -> SpawnRoadAuthoritative).
// Road A is pre-spawned directly so the test isolates SpawnRoadAuthoritative's own behaviour rather
// than depending on Path 1 also being correct.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkDraftCommitCreatesJunctionTest, "TerraDyne.RoadNetwork.DraftCommitCreatesJunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadNetworkDraftCommitCreatesJunctionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	ARealtimeRoadSplineActor* RoadA = SpawnStraightRoad(World, FVector(-1000.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0));
	TestNotNull(TEXT("Pre-existing Road A spawned"), RoadA);
	if (!RoadA) return false;
	TestEqual(TEXT("No junctions yet -- one road alone crosses nothing"), CountJunctions(World), 0);

	ARealtimeRoadEditController* Controller = World->SpawnActor<ARealtimeRoadEditController>();
	if (!Controller || !Controller->RoadBuilder) return false;
	URealtimeRoadBuilderComponent* Builder = Controller->RoadBuilder;
	Builder->bSnapToTerrain = false;

	ARealtimeRoadSplineActor* Draft = Builder->BeginRoadDraft(FVector(0.0, -1000.0, 0.0), ERoadType::PavedRoad);
	TestNotNull(TEXT("Draft road started"), Draft);
	if (!Draft) return false;

	Builder->CommitPoint(FVector(0.0, 1000.0, 0.0));
	ARealtimeRoadSplineActor* RoadB = Builder->FinishRoadDraft();
	TestNotNull(TEXT("Draft committed into a real road"), RoadB);
	if (!RoadB) return false;

	const int32 JunctionCount = CountJunctions(World);
	AddInfo(FString::Printf(TEXT("Junctions after committing a crossing draft road: %d"), JunctionCount));
	TestTrue(TEXT("A junction was auto-created where the committed road crosses the existing one"), JunctionCount >= 1);

	return true;
}

// -----------------------------------------------------------------------------
// Path 3: OSM import (ARealtimeRoadEditController::CreateRoadFromOSM). Same shape as Path 2 --
// Road A pre-spawned directly, Road B created through the fixed entry point.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkOSMImportCreatesJunctionTest, "TerraDyne.RoadNetwork.OSMImportCreatesJunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadNetworkOSMImportCreatesJunctionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	ARealtimeRoadSplineActor* RoadA = SpawnStraightRoad(World, FVector(-1000.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0));
	TestNotNull(TEXT("Pre-existing Road A spawned"), RoadA);
	if (!RoadA) return false;

	ARealtimeRoadEditController* Controller = World->SpawnActor<ARealtimeRoadEditController>();
	TestNotNull(TEXT("Controller spawned"), Controller);
	if (!Controller) return false;

	const TArray<FVector> OSMCentreline = { FVector(0.0, -1000.0, 0.0), FVector(0.0, 1000.0, 0.0) };
	AActor* RoadB = Controller->CreateRoadFromOSM(OSMCentreline, 6.5f);
	TestNotNull(TEXT("CreateRoadFromOSM produced a road"), RoadB);
	if (!RoadB) return false;

	const int32 JunctionCount = CountJunctions(World);
	AddInfo(FString::Printf(TEXT("Junctions after an OSM-imported road crosses an existing one: %d"), JunctionCount));
	TestTrue(TEXT("A junction was auto-created where the OSM-imported road crosses the existing one"), JunctionCount >= 1);

	return true;
}

#endif // WITH_EDITOR
