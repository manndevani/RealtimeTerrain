// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RealtimeRoadSplineActor.h"
#include "RealtimeRoadJunctionActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// End-to-end check that the whole pipeline this session built actually connects: three real roads
// meeting near a point -- a 2-lane road passing straight through plus a 1-lane ramp branching off
// at a shallow 20-degree angle, the textbook highway on/off-ramp shape -- spawned into a world with
// a real junction actor solving from them. Confirms SolveIntersection genuinely detects the ramp
// corner (not just that RoadNetworkSolver::IsRampConnection returns true in isolation, already
// covered elsewhere) and that RebuildJunction's mesh-building actually runs the new gore-fill code
// path without crashing or leaving the mesh empty.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealtimeRoadJunctionActorGoreTest, "TerraDyne.RoadJunctionActor.RampTopologyDetectsGoreCorner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRealtimeRoadJunctionActorGoreTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	auto SpawnRoad = [World](const FVector& From, const FVector& To, ERoadType Type) -> ARealtimeRoadSplineActor*
	{
		ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>();
		if (!Road) return nullptr;
		Road->RoadType = Type;
		Road->bConformToTerrainInRealtime = false; // no ground in this test world.
		Road->bRebaseTerrain = false;
		Road->SetPointsRealtime({ From, To });
		Road->RequestRebuild();
		return Road;
	};

	// A straight-through 2-lane main road (one arm each direction) plus a 1-lane ramp branching
	// off at a shallow angle -- the real topology a highway on/off-ramp has, and exactly the shape
	// TerraDyne.RoadNetwork.RampClassification/RampCornerGetsWiderRadius already verified the
	// solver math for in isolation.
	ARealtimeRoadSplineActor* MainIncoming = SpawnRoad(FVector(-3000.0, 0.0, 0.0), FVector::ZeroVector, ERoadType::PavedRoad);
	ARealtimeRoadSplineActor* MainContinuing = SpawnRoad(FVector::ZeroVector, FVector(3000.0, 0.0, 0.0), ERoadType::PavedRoad);
	const double RampAngleRad = FMath::DegreesToRadians(20.0);
	ARealtimeRoadSplineActor* Ramp = SpawnRoad(FVector::ZeroVector, FVector(3000.0 * FMath::Cos(RampAngleRad), 3000.0 * FMath::Sin(RampAngleRad), 0.0), ERoadType::RoughRoad);

	TestNotNull(TEXT("Incoming main road should spawn"), MainIncoming);
	TestNotNull(TEXT("Continuing main road should spawn"), MainContinuing);
	TestNotNull(TEXT("Ramp road should spawn"), Ramp);
	if (!MainIncoming || !MainContinuing || !Ramp) return false;

	TestEqual(TEXT("Main roads carry 2 driving lanes by default"), MainIncoming->GetDrivingLaneCount(), 2);
	TestEqual(TEXT("Rough ramp carries 1 driving lane by default"), Ramp->GetDrivingLaneCount(), 1);

	ARealtimeRoadJunctionActor* Junction = World->SpawnActor<ARealtimeRoadJunctionActor>();
	TestNotNull(TEXT("Junction actor should spawn"), Junction);
	if (!Junction) return false;

	Junction->SetActorLocation(FVector::ZeroVector);
	Junction->bConformToTerrain = false; // no ground in this test world.
	Junction->RebuildJunction();

	AddInfo(FString::Printf(TEXT("Ramp corners detected: %d"), Junction->GetRampCornerCount()));
	TestTrue(TEXT("Junction should detect at least one ramp corner from the 20-degree/lane-mismatch topology"), Junction->GetRampCornerCount() >= 1);

	UDynamicMeshComponent* JunctionMeshComp = Junction->JunctionMeshComponent;
	TestNotNull(TEXT("Junction mesh component should exist"), JunctionMeshComp);
	if (!JunctionMeshComp) return false;

	const UE::Geometry::FDynamicMesh3* Mesh = JunctionMeshComp->GetMesh();
	TestNotNull(TEXT("Junction should have generated mesh geometry"), Mesh);
	TestTrue(TEXT("Junction mesh should be non-empty (gore-fill code path ran without leaving it empty)"), Mesh && Mesh->TriangleCount() > 0);

	return true;
}
#endif // WITH_EDITOR
