// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RealtimeRoadSplineActor.h"
#include "RoadMeshBuilder.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// End-to-end check that setting StylePreset on a real spawned road actor actually routes mesh
// generation through BuildRoadSectionFromStyle instead of the CrossSectionSettings-driven path --
// not just that the two functions individually produce correct sections (already covered by
// TerraDyne.RoadMeshBuilder.Style*), but that the actor's own GenerateRoadMesh branch picks the
// right one and the resulting mesh is actually as wide as the style says, not the 800cm
// CrossSectionSettings.RoadWidth default a style-preset road ignores for its own geometry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSplineActorStyleWidthTest, "TerraDyne.RoadSplineActor.StylePresetDrivesWidth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSplineActorStyleWidthTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	URoadStylePreset* Style = NewObject<URoadStylePreset>();
	FRoadStyleLaneEntry Left;
	Left.Width = 300.0;
	Left.LaneType = ERealtimeLaneType::Driving;
	Style->LeftLanes.Add(Left);
	FRoadStyleLaneEntry Right;
	Right.Width = 300.0;
	Right.LaneType = ERealtimeLaneType::Driving;
	Style->RightLanes.Add(Right);
	// Total style width (600) is deliberately far from CrossSectionSettings' own 800cm default so
	// the two paths can't be confused for one another by coincidence.

	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>();
	TestNotNull(TEXT("Road actor should spawn"), Road);
	if (!Road) return false;

	Road->bConformToTerrainInRealtime = false; // use the spline's own Z; no ground needed for this check.
	Road->bRebaseTerrain = false;              // no TerraDyne terrain in this world to rebase into.
	Road->StylePreset = Style;

	Road->SetPointsRealtime({
		FVector(-2000.0, 0.0, 0.0),
		FVector(0.0, 0.0, 0.0),
		FVector(2000.0, 0.0, 0.0),
	});
	Road->RequestRebuild();

	UDynamicMeshComponent* RoadMeshComp = Road->RoadMeshComponent;
	TestNotNull(TEXT("Road mesh component should exist"), RoadMeshComp);
	if (!RoadMeshComp) return false;

	const UE::Geometry::FDynamicMesh3* Mesh = RoadMeshComp->GetMesh();
	TestNotNull(TEXT("Road should have generated mesh geometry"), Mesh);
	if (!Mesh || Mesh->VertexCount() == 0) return false;

	double MinY = TNumericLimits<double>::Max();
	double MaxY = TNumericLimits<double>::Lowest();
	for (int32 VidInt : Mesh->VertexIndicesItr())
	{
		const FVector3d V = Mesh->GetVertex(VidInt);
		MinY = FMath::Min(MinY, V.Y);
		MaxY = FMath::Max(MaxY, V.Y);
	}
	const double MeasuredWidth = MaxY - MinY;

	// Style's total width is 600; CrossSectionSettings.RoadWidth defaults to 800. A wide tolerance
	// (not exact-to-the-mm) because SweepSection's frames aren't guaranteed axis-aligned at every
	// station, but tight enough that landing near 800 (the un-styled default) would still fail it.
	TestTrue(
		FString::Printf(TEXT("Mesh width (%f) should match the style's 600cm, not CrossSectionSettings' 800cm default"), MeasuredWidth),
		FMath::Abs(MeasuredWidth - 600.0) < 50.0);
	return true;
}

// Verifies GetDrivingLaneCount, the input RoadNetworkSolver::IsRampConnection classifies a
// junction connection on: a style with a mix of driving/sidewalk lanes counts only the driving
// ones, and an un-styled road falls back to what CrossSectionSettings has always implied (two
// lanes for paved, one for rough) rather than reading as laneless.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSplineActorDrivingLaneCountTest, "TerraDyne.RoadSplineActor.DrivingLaneCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSplineActorDrivingLaneCountTest::RunTest(const FString& Parameters)
{
	ARealtimeRoadSplineActor* PavedNoStyle = NewObject<ARealtimeRoadSplineActor>();
	PavedNoStyle->RoadType = ERoadType::PavedRoad;
	TestEqual(TEXT("Un-styled paved road defaults to 2 driving lanes"), PavedNoStyle->GetDrivingLaneCount(), 2);
	TestEqual(TEXT("Un-styled road's driving half-width matches half CrossSectionSettings.RoadWidth"), PavedNoStyle->GetDrivingSurfaceHalfWidth(), PavedNoStyle->CrossSectionSettings.RoadWidth * 0.5, 0.01);

	ARealtimeRoadSplineActor* RoughNoStyle = NewObject<ARealtimeRoadSplineActor>();
	RoughNoStyle->RoadType = ERoadType::RoughRoad;
	TestEqual(TEXT("Un-styled rough road defaults to 1 driving lane"), RoughNoStyle->GetDrivingLaneCount(), 1);

	URoadStylePreset* Style = NewObject<URoadStylePreset>();
	FRoadStyleLaneEntry Driving;
	Driving.LaneType = ERealtimeLaneType::Driving;
	Driving.Width = 350.0;
	Style->LeftLanes.Add(Driving);
	Style->RightLanes.Add(Driving);
	Style->RightLanes.Add(Driving);
	FRoadStyleLaneEntry Sidewalk;
	Sidewalk.LaneType = ERealtimeLaneType::Sidewalk;
	Sidewalk.Width = 350.0;
	Style->RightLanes.Add(Sidewalk);

	ARealtimeRoadSplineActor* Styled = NewObject<ARealtimeRoadSplineActor>();
	Styled->StylePreset = Style;
	TestEqual(TEXT("Styled road counts only driving lanes, ignoring the sidewalk"), Styled->GetDrivingLaneCount(), 3);
	// 3 driving lanes at 350cm each = 1050cm total driving surface (sidewalk excluded), so half is 525.
	TestEqual(TEXT("Styled road's driving half-width excludes the sidewalk lane"), Styled->GetDrivingSurfaceHalfWidth(), 525.0, 0.01);
	return true;
}

// Verifies ApplyElevationProfile end-to-end on a real actor: a synthetic sawtooth height sequence
// (the same shape TerraDyne.RoadElevationProfile.AutoFlyoverDetection already verified the
// detector against, run through the actual actor API this time) gets smoothed into an S-curve
// bridge rather than left following every zigzag, and a manually-authored HeightKey overriding
// the same stretch takes priority over the automatic one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSplineActorElevationProfileTest, "TerraDyne.RoadSplineActor.ApplyElevationProfile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSplineActorElevationProfileTest::RunTest(const FString& Parameters)
{
	auto BuildSawtoothSequence = [](TArray<FVector>& OutPoints, TArray<double>& OutDistances)
	{
		OutPoints.Reset();
		OutDistances.Reset();
		for (int32 i = 0; i <= 20; ++i)
		{
			const double Dist = i * 100.0;
			double Height = 0.0;
			if (i > 4 && i <= 15)
			{
				Height = (i % 2 == 1) ? 800.0 : 0.0;
			}
			else if (i > 15)
			{
				Height = 800.0;
			}
			OutPoints.Add(FVector(Dist, 0.0, Height));
			OutDistances.Add(Dist);
		}
	};

	{
		ARealtimeRoadSplineActor* Road = NewObject<ARealtimeRoadSplineActor>();
		Road->bAutoFlyover = true;
		Road->MaxComfortableGradeDegrees = 45.0f;
		Road->MinFlyoverRunLength = 500.0f;

		TArray<FVector> Points;
		TArray<double> Distances;
		BuildSawtoothSequence(Points, Distances);
		Road->ApplyElevationProfile(Points, Distances);

		// Station 10 (dist 1000) sits deep in the sawtooth, at the pre-override height of 0 or 800
		// depending on parity -- after bridging, it should read as an intermediate S-curve value,
		// not still be pinned to one of the sawtooth's own extreme values.
		const double BridgedHeight = Points[10].Z;
		AddInfo(FString::Printf(TEXT("Bridged height at station 10 (dist 1000): %.2f"), BridgedHeight));
		TestTrue(TEXT("Auto-flyover smooths the sawtooth: station 10 no longer sits at a raw sawtooth extreme"), BridgedHeight > 50.0 && BridgedHeight < 750.0);

		// Outside the bridged stretch (station 0, well before it) should be untouched.
		TestEqual(TEXT("Station outside the bridge keeps its original height"), Points[0].Z, 0.0, 0.01);
	}
	{
		// Same sawtooth, but with two manually-authored keys bracketing station 10 (dist 800 and
		// 1200, both inside the [400,1500] stretch the automatic detector would otherwise bridge).
		// A user key anywhere inside an auto-detected run suppresses that WHOLE automatic bridge
		// pair (the designer's own intent takes over for that stretch, full stop, rather than
		// trying to splice partial automatic and manual coverage together) -- so the two user keys
		// need to bracket what they actually want smoothed themselves.
		ARealtimeRoadSplineActor* Road = NewObject<ARealtimeRoadSplineActor>();
		Road->bAutoFlyover = true;
		Road->MaxComfortableGradeDegrees = 45.0f;
		Road->MinFlyoverRunLength = 500.0f;
		FRoadHeightKey UserKeyA;
		UserKeyA.Dist = 800.0;
		UserKeyA.Height = 5000.0; // deliberately far from anything the sawtooth or a bridge would produce.
		UserKeyA.Slope = 0.0;
		FRoadHeightKey UserKeyB;
		UserKeyB.Dist = 1200.0;
		UserKeyB.Height = 5000.0;
		UserKeyB.Slope = 0.0;
		Road->HeightKeys.Add(UserKeyA);
		Road->HeightKeys.Add(UserKeyB);

		TArray<FVector> Points;
		TArray<double> Distances;
		BuildSawtoothSequence(Points, Distances);
		Road->ApplyElevationProfile(Points, Distances);

		// Station 10 (dist 1000) sits between the two user keys, both at height 5000 -- the
		// automatic bridge for this stretch should be suppressed entirely (not partially merged),
		// leaving only the user's own flat 5000 profile in effect there.
		AddInfo(FString::Printf(TEXT("Height at station 10 with a bracketing user override present: %.2f"), Points[10].Z));
		TestEqual(TEXT("A manually-authored bracket overrides the automatic bridge, not just nudges it"), Points[10].Z, 5000.0, 0.01);
	}
	return true;
}
#endif // WITH_EDITOR
