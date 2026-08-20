// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"

// Regression test for self-intersecting curbs/footpaths on tight curves: the roadside band used to
// sweep at a fixed Lateral independently at every station -- a textbook per-vertex-normal offset,
// guaranteed to bowtie through itself on the inside of any turn tighter than the band is wide.
// ComputeBandScaleFromCurvature (used by SweepSection's BandInnerHalfWidth/BandScalePerFrame) fixes
// this by narrowing the band exactly where the local turn radius can't carry the full width, the same
// way ARealtimeRoadJunctionActor already does for a junction corner.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderBandScaleTest, "TerraDyne.RoadMesh.BandScaleFromCurvature", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadMeshBuilderBandScaleTest::RunTest(const FString& Parameters)
{
	// A sharp ~90 degree corner over short (300cm) arms -- exactly the kind of tight turn that would
	// bowtie a fixed-width band.
	const TArray<FVector> TightPoints = {
		FVector(0.0, 0.0, 0.0),
		FVector(300.0, 0.0, 0.0),
		FVector(300.0, 300.0, 0.0),
	};

	TArray<RoadMesh::FFrame> TightFrames;
	TestTrue("BuildFramesFromPolyline should succeed for the tight-corner polyline", RoadMesh::BuildFramesFromPolyline(TightPoints, TightFrames));
	TestEqual("Tight polyline produces one frame per point", TightFrames.Num(), TightPoints.Num());

	// Deliberately wider than this corner's local radius can carry.
	const double RequestedBandWidth = 1000.0;
	TArray<float> TightScale;
	RoadMesh::ComputeBandScaleFromCurvature(TightFrames, RequestedBandWidth, RoadMesh::BandRadiusSafetyFactor, TightScale);

	TestEqual("One scale entry per frame", TightScale.Num(), TightFrames.Num());
	if (TightScale.Num() == TightFrames.Num() && TightScale.Num() == 3)
	{
		AddInfo(FString::Printf(TEXT("Tight-corner band scale at the corner station: %.3f"), TightScale[1]));
		TestTrue("The corner station's band scale should be clamped well below 1", TightScale[1] < 0.6f);
		TestTrue("The corner station's band scale should still be positive", TightScale[1] > 0.0f);
		TestEqual("The first (endpoint) station is left at full scale", TightScale[0], 1.0f);
		TestEqual("The last (endpoint) station is left at full scale", TightScale[2], 1.0f);
	}

	// Sanity check the other direction: a gently curving, realistic road should NOT have its band
	// narrowed at all. A false-positive clamp here would visibly pinch curbs on completely normal
	// roads, which is its own regression.
	TArray<FVector> GentlePoints;
	GentlePoints.Reserve(9);
	for (int32 i = 0; i < 9; ++i)
	{
		// A broad, gentle arc: 8 degrees of turn per 500cm step, roughly a 3500cm radius curve --
		// comfortably wider than any realistic curb+footpath band.
		const double AngleRad = FMath::DegreesToRadians(8.0 * i);
		GentlePoints.Add(FVector(3500.0 * FMath::Sin(AngleRad), 3500.0 * (1.0 - FMath::Cos(AngleRad)), 0.0));
	}

	TArray<RoadMesh::FFrame> GentleFrames;
	TestTrue("BuildFramesFromPolyline should succeed for the gentle-arc polyline", RoadMesh::BuildFramesFromPolyline(GentlePoints, GentleFrames));

	TArray<float> GentleScale;
	RoadMesh::ComputeBandScaleFromCurvature(GentleFrames, RequestedBandWidth, RoadMesh::BandRadiusSafetyFactor, GentleScale);

	bool bAnyGentleClamped = false;
	for (const float Scale : GentleScale)
	{
		if (Scale < 0.99f)
		{
			bAnyGentleClamped = true;
			break;
		}
	}
	TestFalse("A gentle, realistic curve should not have its band scale clamped at all", bAnyGentleClamped);

	// And the no-op path: RequestedBandWidth of 0 (a rough road with no curb/footpath band at all)
	// should never clamp anything, regardless of how tight the curve is.
	TArray<float> NoBandScale;
	RoadMesh::ComputeBandScaleFromCurvature(TightFrames, 0.0, RoadMesh::BandRadiusSafetyFactor, NoBandScale);
	bool bAnyNoBandClamped = false;
	for (const float Scale : NoBandScale)
	{
		if (Scale < 0.99f)
		{
			bAnyNoBandClamped = true;
			break;
		}
	}
	TestFalse("Zero requested band width should never clamp, even on a tight corner", bAnyNoBandClamped);

	return true;
}
