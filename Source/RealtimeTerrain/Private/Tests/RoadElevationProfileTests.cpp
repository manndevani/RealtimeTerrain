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
	{
		// Regression: two keys authored at the exact same Dist (a duplicate/degenerate key, e.g. two
		// separately-authored points that both landed on the same station) used to divide 0/0,
		// contaminating the result with NaN -- a single non-finite height silently discards every
		// mesh triangle touching that vertex with no error. Bracketing this degenerate pair between
		// two real keys also confirms the fix doesn't just special-case a 2-key list.
		TArray<FRoadHeightKey> DuplicateDist;
		DuplicateDist.Add({ 0.0, 0.0, 0.0 });
		DuplicateDist.Add({ 1000.0, 500.0, 0.0 });
		DuplicateDist.Add({ 1000.0, 500.0, 0.0 });
		DuplicateDist.Add({ 2000.0, 800.0, 0.0 });
		const double AtDuplicate = RoadElevationProfile::EvaluateHeight(DuplicateDist, 1000.0);
		TestTrue(TEXT("Zero-length segment does not produce NaN"), FMath::IsFinite(AtDuplicate));
		TestEqual(TEXT("Zero-length segment evaluates to the segment's own (equal) height"), AtDuplicate, 500.0, 0.01);
	}
	return true;
}

// Verifies RoadElevationProfile::DetectFlyoverBridges -- the automatic flyover trigger. Builds a
// synthetic height sequence that is perfectly flat (0-degree grade) for the first few stations,
// then a steep sawtooth (~83-degree grade every step, an artificially extreme case chosen so
// there's no ambiguity about which stations count as "rough") for a stretch long enough to
// qualify, then flat again -- and confirms a bridge is detected spanning exactly the rough
// stretch's boundary distances/heights, not some other interval.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadElevationProfileFlyoverDetectionTest, "TerraDyne.RoadElevationProfile.AutoFlyoverDetection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadElevationProfileFlyoverDetectionTest::RunTest(const FString& Parameters)
{
	const double MaxGradeRadians = FMath::DegreesToRadians(45.0);
	const double MinRunLength = 500.0;

	{
		// Flat 0-4 (dist 0-400), sawtooth 5-15 (dist 500-1500, alternating 0/800 -- ~83deg grade
		// every step), flat again at 800 from 16-20 (dist 1600-2000).
		TArray<double> Distances;
		TArray<double> Heights;
		for (int32 i = 0; i <= 20; ++i)
		{
			Distances.Add(i * 100.0);
			if (i <= 4) Heights.Add(0.0);
			else if (i <= 15) Heights.Add((i % 2 == 1) ? 800.0 : 0.0);
			else Heights.Add(800.0);
		}

		TArray<FRoadHeightKey> Bridges;
		RoadElevationProfile::DetectFlyoverBridges(Distances, Heights, MaxGradeRadians, MinRunLength, Bridges);

		TestEqual(TEXT("Exactly one bridge (2 keys) detected"), Bridges.Num(), 2);
		if (Bridges.Num() == 2)
		{
			TestEqual(TEXT("Bridge starts where the flat run ends (dist 400)"), Bridges[0].Dist, 400.0, 0.01);
			TestEqual(TEXT("Bridge start height matches the flat ground there"), Bridges[0].Height, 0.0, 0.01);
			TestEqual(TEXT("Bridge ends where the sawtooth settles back to flat (dist 1500)"), Bridges[1].Dist, 1500.0, 0.01);
			TestEqual(TEXT("Bridge end height matches the settled height there"), Bridges[1].Height, 800.0, 0.01);
			TestEqual(TEXT("Bridge keys have zero slope (smooth departure/return grade)"), Bridges[0].Slope, 0.0, 0.001);
		}
	}
	{
		// Perfectly flat throughout -- no grade ever exceeds the threshold, so no bridge at all.
		TArray<double> Distances = { 0.0, 500.0, 1000.0, 1500.0, 2000.0 };
		TArray<double> Heights = { 100.0, 100.0, 100.0, 100.0, 100.0 };
		TArray<FRoadHeightKey> Bridges;
		RoadElevationProfile::DetectFlyoverBridges(Distances, Heights, MaxGradeRadians, MinRunLength, Bridges);
		TestEqual(TEXT("Flat sequence produces no bridges"), Bridges.Num(), 0);
	}
	{
		// A single steep step (one station pair) that's too SHORT to qualify as a genuine flyover
		// stretch -- MinRunLength should gate it out rather than bridging every single bump.
		TArray<double> Distances = { 0.0, 100.0, 200.0, 300.0 };
		TArray<double> Heights = { 0.0, 800.0, 800.0, 800.0 };
		TArray<FRoadHeightKey> Bridges;
		RoadElevationProfile::DetectFlyoverBridges(Distances, Heights, MaxGradeRadians, MinRunLength, Bridges);
		TestEqual(TEXT("A single too-short steep step does not trigger a bridge"), Bridges.Num(), 0);
	}
	return true;
}
