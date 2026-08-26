// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadElevationProfile.h"

// Verifies the cubic-Hermite height math ported from RoadBuilder's FHeightSegment: a flat
// two-key profile evaluates to a constant, and a genuine climb between two zero-slope keys
// produces a real S-curve (not linear interpolation) -- the same properties
// TerraDyne.RoadCurveGraph.OffsetMath already verified for the lateral-offset version of this
// exact math, checked again here since FRoadHeightKey is a distinct struct even though it shares
// RoadCurveGraph::FindElementIndex's binary search.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadElevationProfileMathTest, "TerraDyne.RoadElevationProfile.HeightMath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadElevationProfileMathTest::RunTest(const FString& Parameters)
{
	{
		TArray<FRoadHeightKey> Flat;
		Flat.Add({ 0.0, 500.0, 0.0 });
		Flat.Add({ 1000.0, 500.0, 0.0 });
		TestEqual(TEXT("Flat profile start"), RoadElevationProfile::EvaluateHeight(Flat, 0.0), 500.0, 0.01);
		TestEqual(TEXT("Flat profile midpoint"), RoadElevationProfile::EvaluateHeight(Flat, 500.0), 500.0, 0.01);
		TestEqual(TEXT("Flat profile end"), RoadElevationProfile::EvaluateHeight(Flat, 1000.0), 500.0, 0.01);
	}
	{
		// A climb from ground level to a 20m-high flyover deck, zero slope at both ends (a smooth
		// on-ramp grade, not a kinked step).
		TArray<FRoadHeightKey> Climb;
		Climb.Add({ 0.0, 0.0, 0.0 });
		Climb.Add({ 2000.0, 2000.0, 0.0 });
		TestEqual(TEXT("Climb start at ground level"), RoadElevationProfile::EvaluateHeight(Climb, 0.0), 0.0, 0.01);
		TestEqual(TEXT("Climb end at deck height"), RoadElevationProfile::EvaluateHeight(Climb, 2000.0), 2000.0, 0.01);
		TestEqual(TEXT("Climb midpoint (S-curve symmetry)"), RoadElevationProfile::EvaluateHeight(Climb, 1000.0), 1000.0, 0.01);
		// Quarter-point lags behind the straight-line value (500) because the climb starts flat --
		// this is what distinguishes it from a linear grade.
		const double QuarterPoint = RoadElevationProfile::EvaluateHeight(Climb, 500.0);
		TestTrue(TEXT("Climb quarter-point is not linear"), QuarterPoint < 500.0);
	}
	{
		// Three keys: confirms the bracket search picks the right segment, not just "the first
		// and last key" -- a flyover climbing then holding a level deck before descending again.
		TArray<FRoadHeightKey> ClimbHoldDescend;
		ClimbHoldDescend.Add({ 0.0, 0.0, 0.0 });
		ClimbHoldDescend.Add({ 1000.0, 1500.0, 0.0 });
		ClimbHoldDescend.Add({ 2000.0, 0.0, 0.0 });
		TestEqual(TEXT("Three-key profile: deck height at the middle key"), RoadElevationProfile::EvaluateHeight(ClimbHoldDescend, 1000.0), 1500.0, 0.01);
		TestEqual(TEXT("Three-key profile: back to ground at the far end"), RoadElevationProfile::EvaluateHeight(ClimbHoldDescend, 2000.0), 0.0, 0.01);
		TestTrue(TEXT("Three-key profile: partway up the climb is between ground and deck height"), RoadElevationProfile::EvaluateHeight(ClimbHoldDescend, 500.0) > 0.0 && RoadElevationProfile::EvaluateHeight(ClimbHoldDescend, 500.0) < 1500.0);
	}
	return true;
}
