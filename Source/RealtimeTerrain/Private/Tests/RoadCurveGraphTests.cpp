// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadCurveGraph.h"

// Verifies the cubic-Hermite offset/heading math ported from RoadBuilder's FCurveOffset: a flat
// two-key profile evaluates to a constant, and a profile with a genuine slope change actually
// curves (matches neither key's raw value at the midpoint) rather than silently degrading to
// linear interpolation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCurveGraphOffsetMathTest, "TerraDyne.RoadCurveGraph.OffsetMath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadCurveGraphOffsetMathTest::RunTest(const FString& Parameters)
{
	{
		TArray<FRoadCurveOffsetKey> Flat;
		Flat.Add({ 0.0, 400.0, 0.0 });
		Flat.Add({ 1000.0, 400.0, 0.0 });
		TestEqual(TEXT("Flat profile midpoint"), RoadCurveGraph::EvaluateOffset(Flat, 500.0), 400.0, 0.01);
		TestEqual(TEXT("Flat profile start"), RoadCurveGraph::EvaluateOffset(Flat, 0.0), 400.0, 0.01);
		TestEqual(TEXT("Flat profile end"), RoadCurveGraph::EvaluateOffset(Flat, 1000.0), 400.0, 0.01);
	}
	{
		// A ramp taper: 400 -> 200 over 1000cm, with zero slope at both ends (an S-curve, not a
		// straight taper). The midpoint of a true cubic S-curve with these boundary conditions
		// sits exactly on the straight-line average by symmetry, so this checks the curve is
		// actually smooth (zero-slope Hermite) rather than that it merely reaches the endpoints.
		TArray<FRoadCurveOffsetKey> Taper;
		Taper.Add({ 0.0, 400.0, 0.0 });
		Taper.Add({ 1000.0, 200.0, 0.0 });
		TestEqual(TEXT("Taper start"), RoadCurveGraph::EvaluateOffset(Taper, 0.0), 400.0, 0.01);
		TestEqual(TEXT("Taper end"), RoadCurveGraph::EvaluateOffset(Taper, 1000.0), 200.0, 0.01);
		TestEqual(TEXT("Taper midpoint (S-curve symmetry)"), RoadCurveGraph::EvaluateOffset(Taper, 500.0), 300.0, 0.01);
		// Quarter-point of a zero-slope-ended S-curve lags behind the straight-line interpolation
		// (350) because the curve starts flat -- this is what distinguishes it from a linear taper.
		const double QuarterPoint = RoadCurveGraph::EvaluateOffset(Taper, 250.0);
		TestTrue(TEXT("Taper quarter-point is not linear"), QuarterPoint > 350.0);
	}
	return true;
}

// Verifies Cut() (ported from URoadCurve::Cut) trims and re-bases LocalOffsets to the requested
// sub-range without disturbing the curve's authored shape inside that range.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCurveGraphCutTest, "TerraDyne.RoadCurveGraph.Cut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadCurveGraphCutTest::RunTest(const FString& Parameters)
{
	URealtimeRoadCurve* Curve = NewObject<URealtimeRoadCurve>();
	Curve->LocalOffsets.Add({ 0.0, 400.0, 0.0 });
	Curve->LocalOffsets.Add({ 500.0, 400.0, 0.0 });
	Curve->LocalOffsets.Add({ 1000.0, 400.0, 0.0 });
	Curve->RecomputeEffectiveOffsets();

	Curve->Cut(200.0, 700.0);
	Curve->RecomputeEffectiveOffsets();

	TestEqual(TEXT("Cut re-based length"), Curve->Length(), 500.0, 0.01);
	TestEqual(TEXT("Cut preserved value at new start"), Curve->GetOffset(0.0), 400.0, 0.01);
	TestEqual(TEXT("Cut preserved value at new end"), Curve->GetOffset(500.0), 400.0, 0.01);
	// The surviving interior key (originally at 500) should now sit at 300 post-rebase.
	bool bFoundInteriorKey = false;
	for (const FRoadCurveOffsetKey& Key : Curve->LocalOffsets)
	{
		if (FMath::IsNearlyEqual(Key.Dist, 300.0, 1.0))
		{
			bFoundInteriorKey = true;
		}
	}
	TestTrue(TEXT("Interior key survived re-based at 300"), bFoundInteriorKey);
	return true;
}

// Verifies Join() (ported from URoadCurve::Join) appends a second curve's keys after the first's
// own end, distance-shifted to continue seamlessly rather than overlapping or leaving a gap.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCurveGraphJoinTest, "TerraDyne.RoadCurveGraph.Join", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadCurveGraphJoinTest::RunTest(const FString& Parameters)
{
	URealtimeRoadCurve* First = NewObject<URealtimeRoadCurve>();
	First->LocalOffsets.Add({ 0.0, 400.0, 0.0 });
	First->LocalOffsets.Add({ 1000.0, 400.0, 0.0 });
	First->RecomputeEffectiveOffsets();

	URealtimeRoadCurve* Second = NewObject<URealtimeRoadCurve>();
	Second->LocalOffsets.Add({ 0.0, 400.0, 0.0 });
	Second->LocalOffsets.Add({ 500.0, 300.0, 0.0 });
	Second->RecomputeEffectiveOffsets();

	First->Join(Second);
	First->RecomputeEffectiveOffsets();

	TestEqual(TEXT("Joined key count"), First->LocalOffsets.Num(), 2);
	TestEqual(TEXT("Joined total length"), First->Length(), 1500.0, 0.01);
	TestEqual(TEXT("Joined tail value"), First->GetOffset(1500.0), 300.0, 0.01);
	return true;
}
