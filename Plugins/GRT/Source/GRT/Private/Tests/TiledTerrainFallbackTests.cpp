// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTManager.h"
#include "Building/GRTBuildingActor.h"
#include "Components/BoxComponent.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// Exercises the building pad-height raycast fallback for "buildings on a locally-imported Cesium
// tile" (ground with no GRT chunk under it). None of this needs a real Cesium3DTileset: a
// plain collidable UBoxComponent on ECC_WorldStatic is indistinguishable from Cesium's own tile
// collision as far as a line trace is concerned, and is far cheaper and more precise to set up in
// a test.
namespace TiledTerrainFallbackTest_Private
{
	/** A bare actor with one large, flat, WorldStatic-blocking box -- a stand-in for "some collidable
	 * ground with no GRT chunk under it", positioned so its top face sits at TopZ. */
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingPadRaycastFallbackTest,
	"GRT.WorldFramework.BuildingPadRaycastFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingPadRaycastFallbackTest::RunTest(const FString& Parameters)
{
	using namespace TiledTerrainFallbackTest_Private;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	// A manager is required (FindManager() gates on it) but deliberately has no chunks -- the whole
	// point is proving the pad height comes from the raycast fallback, not the chunk buffer.
	AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
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
	AGRTBuildingActor* Building = World->SpawnActor<AGRTBuildingActor>(
		AGRTBuildingActor::StaticClass(), FTransform(FVector(0.0, 0.0, 99999.0)));
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

#endif // WITH_EDITOR
