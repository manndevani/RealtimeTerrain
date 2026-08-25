// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"
#include "RealtimeRoadSplineActor.h"
#include "Core/TerraDyneManager.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// Exercises the three places this session's "roads/buildings on a locally-imported Cesium tile"
// work touches: RoadMesh::SampleGroundHeight's new fallback-source reporting, the building
// pad-height raycast fallback it enables, and paved-road cross-slope banking that engages only
// where that fallback fired (i.e. ground with no TerraDyne chunk under it -- in practice a Cesium
// tile). None of this needs a real Cesium3DTileset: a plain collidable UBoxComponent on
// ECC_WorldStatic is indistinguishable from Cesium's own tile collision as far as a line trace is
// concerned, and is far cheaper and more precise to set up in a test.
namespace TiledTerrainFallbackTest_Private
{
	/** A bare actor with one large, flat, WorldStatic-blocking box -- a stand-in for "some collidable
	 * ground with no TerraDyne chunk under it", positioned so its top face sits at TopZ. */
	AActor* SpawnGroundBox(UWorld* World, const FVector2D& CenterXY, double TopZ, double HalfExtentXY = 2000.0)
	{
		AActor* GroundActor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(GroundActor);
		Box->SetBoxExtent(FVector(HalfExtentXY, HalfExtentXY, 50.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		GroundActor->SetRootComponent(Box);
		Box->RegisterComponent();
		GroundActor->SetActorLocation(FVector(CenterXY.X, CenterXY.Y, TopZ - 50.0));
		return GroundActor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSampleGroundHeightFallbackSourceTest,
	"TerraDyne.RoadMesh.SampleGroundHeightFallbackSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSampleGroundHeightFallbackSourceTest::RunTest(const FString& Parameters)
{
	using namespace TiledTerrainFallbackTest_Private;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	AActor* Context = World->SpawnActor<AActor>();
	TestNotNull(TEXT("Context actor should spawn"), Context);
	if (!Context) return false;

	// No ATerraDyneManager in this world at all -- SampleGroundHeight must fall all the way through
	// to the raycast, which is exactly the "no TerraDyne chunk here" case a Cesium tile produces.
	AActor* Ground = SpawnGroundBox(World, FVector2D(0.0, 0.0), 550.0);
	TestNotNull(TEXT("Ground box should spawn"), Ground);
	if (!Ground) return false;

	TWeakObjectPtr<ATerraDyneManager> ManagerCache;
	float HeightZ = 0.0f;
	bool bWasFallback = false;
	const bool bSampled = RoadMesh::SampleGroundHeight(Context, ManagerCache, FVector2D(0.0, 0.0), HeightZ, &bWasFallback);

	TestTrue(TEXT("SampleGroundHeight should succeed via the raycast fallback"), bSampled);
	TestTrue(TEXT("OutWasFallback should be true when no TerraDyne chunk covers the point"), bWasFallback);
	TestTrue(TEXT("Sampled height should match the ground box's top face"), FMath::IsNearlyEqual(HeightZ, 550.0f, 1.0f));

	// Nothing collidable at all here -- must fail cleanly, not report a stale/zero height as if it
	// were a real fallback sample.
	float MissHeight = 0.0f;
	bool bMissWasFallback = true;
	const bool bMissed = RoadMesh::SampleGroundHeight(Context, ManagerCache, FVector2D(50000.0, 50000.0), MissHeight, &bMissWasFallback);
	TestFalse(TEXT("SampleGroundHeight should fail where nothing collidable exists"), bMissed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingPadRaycastFallbackTest,
	"TerraDyne.WorldFramework.BuildingPadRaycastFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingPadRaycastFallbackTest::RunTest(const FString& Parameters)
{
	using namespace TiledTerrainFallbackTest_Private;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	// A manager is required (FindManager() gates on it) but deliberately has no chunks -- the whole
	// point is proving the pad height comes from the raycast fallback, not the chunk buffer.
	ATerraDyneManager* Mgr = World->SpawnActor<ATerraDyneManager>();
	TestNotNull(TEXT("Manager instance should be valid"), Mgr);
	if (!Mgr) return false;
	Mgr->bShowWorldModeChoiceOnBeginPlay = false;

	const double GroundTopZ = 4200.0;
	AActor* Ground = SpawnGroundBox(World, FVector2D(0.0, 0.0), GroundTopZ);
	TestNotNull(TEXT("Ground box should spawn"), Ground);
	if (!Ground) return false;

	// Spawned well above the synthetic ground, at a Z that must NOT survive into the pad height --
	// if ResolvePadTargetWorldZ silently fell back to GetActorLocation().Z (the old "no chunk" path)
	// instead of actually tracing, this test would still pass by coincidence unless the spawn height
	// is clearly different from the ground height.
	ATerraDyneBuildingActor* Building = World->SpawnActor<ATerraDyneBuildingActor>(
		ATerraDyneBuildingActor::StaticClass(), FTransform(FVector(0.0, 0.0, 99999.0)));
	TestNotNull(TEXT("Building should spawn"), Building);
	if (!Building) return false;

	Building->bAutoPadHeight = true;
	Building->RelevelToTerrain();
	const float PadZ = Building->ResolvePadTargetWorldZ();

	TestTrue(
		FString::Printf(TEXT("Pad height should come from the raycast fallback (ground top %.1f), not the building's spawn Z (got %.1f)"), GroundTopZ, PadZ),
		FMath::IsNearlyEqual(PadZ, (float)GroundTopZ, 5.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPavedRoadCesiumBankingTest,
	"TerraDyne.RoadNetwork.PavedRoadBanksOverNonTerraDyneGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPavedRoadCesiumBankingTest::RunTest(const FString& Parameters)
{
	using namespace TiledTerrainFallbackTest_Private;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	// One box, rolled about the route's own +X direction, stands in for a single tilted Cesium tile:
	// a real flat surface whose height varies linearly across Y. Large enough in X and Y to cover the
	// whole route AND both lateral cross-slope sample offsets, so every raycast SampleGroundHeight's
	// fallback needs -- the centreline resample, and the two ComputeCrossSlopeRoll side samples --
	// lands on the SAME tilted plane, exactly like one real tilted terrain tile would present.
	const double RollDegrees = 20.0;
	AActor* Ground = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Ground);
	Box->SetBoxExtent(FVector(3000.0, 1200.0, 50.0));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Ground->SetRootComponent(Box);
	Box->RegisterComponent();
	Ground->SetActorLocation(FVector(0.0, 0.0, 1000.0));
	Ground->SetActorRotation(FRotator(0.0, 0.0, RollDegrees));

	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>();
	TestNotNull(TEXT("Road actor should spawn"), Road);
	if (!Road) return false;

	Road->RoadType = ERoadType::PavedRoad;
	Road->bConformToTerrainInRealtime = true; // ComputeCrossSlopeRoll no-ops entirely otherwise.
	Road->bRebaseTerrain = false;              // no TerraDyne terrain in this world to rebase into.

	Road->SetPointsRealtime({
		FVector(-2000.0, 0.0, 1000.0),
		FVector(0.0, 0.0, 1000.0),
		FVector(2000.0, 0.0, 1000.0),
	});
	Road->RequestRebuild();

	UDynamicMeshComponent* RoadMeshComp = Road->RoadMeshComponent;
	TestNotNull(TEXT("Road mesh component should exist"), RoadMeshComp);
	if (!RoadMeshComp)
	{
		return false;
	}

	const UE::Geometry::FDynamicMesh3* Mesh = RoadMeshComp->GetMesh();
	TestNotNull(TEXT("Road should have generated mesh geometry"), Mesh);
	if (!Mesh || Mesh->VertexCount() == 0)
	{
		return false;
	}

	// Don't anchor to a specific station -- polyline resampling doesn't guarantee a ring lands at any
	// particular X. Instead, average Z separately for vertices left/right of the centreline across
	// the WHOLE mesh (away from the end caps): a banked cross-section trends one side higher across
	// every station, while a flat one (only a symmetric camber crown, unaffected by Y sign) averages
	// the same on both sides.
	double LeftSum = 0.0;
	int32 LeftCount = 0;
	double RightSum = 0.0;
	int32 RightCount = 0;
	for (int32 VidInt : Mesh->VertexIndicesItr())
	{
		const FVector3d V = Mesh->GetVertex(VidInt);
		// RoadMeshComponent's vertices are in the component's local space (WorldToLocal was applied
		// during SweepSection) -- the component itself sits at the actor's identity transform here,
		// so local and world coordinates coincide for this test's purposes.
		if (FMath::Abs(V.X) > 1500.0)
		{
			continue; // stay clear of the start/end caps
		}
		if (V.Y < -100.0)
		{
			LeftSum += V.Z;
			++LeftCount;
		}
		else if (V.Y > 100.0)
		{
			RightSum += V.Z;
			++RightCount;
		}
	}

	TestTrue(TEXT("Should find road vertices on both sides of the centreline"), LeftCount > 0 && RightCount > 0);
	if (LeftCount == 0 || RightCount == 0)
	{
		return false;
	}

	const double LeftAvgZ = LeftSum / LeftCount;
	const double RightAvgZ = RightSum / RightCount;
	TestTrue(
		FString::Printf(TEXT("Paved road over a tilted non-TerraDyne surface should bank (left avg Z %.1f, right avg Z %.1f)"), LeftAvgZ, RightAvgZ),
		FMath::Abs(RightAvgZ - LeftAvgZ) > 20.0);

	return true;
}

#endif // WITH_EDITOR
