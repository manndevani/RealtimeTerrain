// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"

// Regression test for the near-vertical cross-section frame discontinuity: BuildFramesFromPolyline
// used to re-derive Right from World Up at every station, with a hard, unblended fallback to
// ForwardVector-cross-Tangent the instant the tangent went near-vertical. A road climbing (or
// switchbacking) through vertical hit that fallback partway along its length, and the cross-section
// visibly flipped ~180 degrees between two adjacent stations. Fixed by propagating a
// rotation-minimizing frame station-to-station instead of re-deriving it from a fixed reference
// vector each time.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderNearVerticalFrameTest, "TerraDyne.RoadMesh.NearVerticalFrameContinuity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadMeshBuilderNearVerticalFrameTest::RunTest(const FString& Parameters)
{
	// A route that climbs essentially straight up for a long stretch, then levels back out. The
	// interior points (index 1 and 2) have a central-difference tangent within a hair of pure
	// vertical -- exactly the case the old per-station World-Up re-derivation broke on.
	const TArray<FVector> Points = {
		FVector(0.0, 0.0, 0.0),
		FVector(100.0, 0.0, 100.0),
		FVector(100.0, 0.0, 10100.0),
		FVector(100.0, 0.0, 20100.0),
		FVector(200.0, 0.0, 20200.0),
	};

	TArray<RoadMesh::FFrame> Frames;
	const bool bBuilt = RoadMesh::BuildFramesFromPolyline(Points, Frames);
	TestTrue("BuildFramesFromPolyline should succeed for a valid polyline", bBuilt);
	TestEqual("One frame per input point", Frames.Num(), Points.Num());
	if (!bBuilt || Frames.Num() != Points.Num())
	{
		return false;
	}

	// Every frame's axes should stay orthonormal and right-handed regardless of how the tangent
	// direction moves through the climb.
	for (int32 i = 0; i < Frames.Num(); ++i)
	{
		const RoadMesh::FFrame& F = Frames[i];
		TestTrue(FString::Printf(TEXT("Frame %d: Tangent is unit length"), i), FMath::IsNearlyEqual(F.Tangent.Size(), 1.0, 0.01));
		TestTrue(FString::Printf(TEXT("Frame %d: Right is unit length"), i), FMath::IsNearlyEqual(F.Right.Size(), 1.0, 0.01));
		TestTrue(FString::Printf(TEXT("Frame %d: Right is perpendicular to Tangent"), i), FMath::IsNearlyZero(FVector::DotProduct(F.Tangent, F.Right), 0.01));
	}

	// The actual regression check: consecutive frames' Right vectors must not flip. A discontinuous
	// hard-switch (the old bug) shows up as a dot product near -1 between adjacent stations; a
	// smoothly propagated frame stays close to +1 even while the tangent itself is swinging through
	// vertical, since Right is only ever turning to stay perpendicular to Tangent, never restarting
	// from a fixed reference axis.
	for (int32 i = 1; i < Frames.Num(); ++i)
	{
		const double Continuity = FVector::DotProduct(Frames[i - 1].Right, Frames[i].Right);
		TestTrue(
			FString::Printf(TEXT("Frame %d->%d: Right should not flip (dot=%.3f)"), i - 1, i, Continuity),
			Continuity > 0.5);
	}

	return true;
}
