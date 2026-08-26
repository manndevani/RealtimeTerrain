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

// Verifies a lane's segment-list Cut/Join (ported from URoadBuilderLane) trims/appends by
// segment only -- a lane's own inherited curve offsets are unused, matching RoadBuilder, where a
// lane's shape comes from its two boundaries rather than its own curve data.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCurveGraphLaneSegmentTest, "TerraDyne.RoadCurveGraph.LaneSegments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadCurveGraphLaneSegmentTest::RunTest(const FString& Parameters)
{
	URealtimeRoadLane* Lane = NewObject<URealtimeRoadLane>();
	Lane->LocalOffsets.Add({ 0.0, 0.0, 0.0 });
	Lane->LocalOffsets.Add({ 1000.0, 0.0, 0.0 });
	Lane->RecomputeEffectiveOffsets();
	Lane->Segments.Add({ 0.0, ERealtimeLaneType::Driving });
	Lane->Segments.Add({ 500.0, ERealtimeLaneType::Sidewalk });

	TestEqual(TEXT("Segment at 250 is Driving"), (int32)Lane->Segments[Lane->GetSegmentAt(250.0)].LaneType, (int32)ERealtimeLaneType::Driving);
	TestEqual(TEXT("Segment at 750 is Sidewalk"), (int32)Lane->Segments[Lane->GetSegmentAt(750.0)].LaneType, (int32)ERealtimeLaneType::Sidewalk);

	Lane->Cut(250.0, 750.0);
	TestEqual(TEXT("Cut lane segment count"), Lane->Segments.Num(), 2);
	// Ported Cut() filters by whether a segment's [Start,End) still overlaps the cut range and
	// then rebases every surviving segment's Dist by -Start; it does not clip a segment's own Dist
	// to the range boundary. The first segment originally started at 0 (before the cut range) but
	// still overlaps it, so it survives with a negative rebased Dist rather than being snapped to 0.
	TestEqual(TEXT("Cut lane first segment rebased (started before range)"), Lane->Segments[0].Dist, -250.0, 0.01);
	TestEqual(TEXT("Cut lane second segment re-based to 250"), Lane->Segments[1].Dist, 250.0, 0.01);

	URealtimeRoadLane* Other = NewObject<URealtimeRoadLane>();
	Other->Segments.Add({ 0.0, ERealtimeLaneType::Sidewalk });
	Other->Segments.Add({ 100.0, ERealtimeLaneType::Median });
	Lane->Join(Other);
	TestEqual(TEXT("Joined lane segment count"), Lane->Segments.Num(), 3);
	// Join's distance-shift is Length(), which (matching RoadBuilder) reads the lane's own curve
	// Offsets rather than its segment list -- Cut() above never touched Offsets, so Length() is
	// still the original 1000, not the post-cut segment span.
	TestEqual(TEXT("Joined lane tail segment re-based by curve Length()"), Lane->Segments.Last().Dist, 1000.0 + 100.0, 0.01);
	TestEqual(TEXT("Joined lane tail segment type"), (int32)Lane->Segments.Last().LaneType, (int32)ERealtimeLaneType::Median);
	return true;
}

// Verifies a boundary's Cut/Join trims/joins both the curve offsets AND the segment list
// together (unlike a lane), matching RoadBuilder's URoadBoundary::Cut/Join which both call
// URoadCurve::Cut/Join before touching Segments.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCurveGraphBoundarySegmentTest, "TerraDyne.RoadCurveGraph.BoundarySegments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadCurveGraphBoundarySegmentTest::RunTest(const FString& Parameters)
{
	URealtimeRoadBoundary* Boundary = NewObject<URealtimeRoadBoundary>();
	Boundary->LocalOffsets.Add({ 0.0, 400.0, 0.0 });
	Boundary->LocalOffsets.Add({ 1000.0, 400.0, 0.0 });
	Boundary->RecomputeEffectiveOffsets();
	Boundary->Segments.Add({ 0.0, false });
	Boundary->Segments.Add({ 500.0, true });

	Boundary->Cut(250.0, 750.0);
	Boundary->RecomputeEffectiveOffsets();

	TestEqual(TEXT("Cut boundary curve length"), Boundary->Length(), 500.0, 0.01);
	TestEqual(TEXT("Cut boundary offset preserved"), Boundary->GetOffset(0.0), 400.0, 0.01);
	TestEqual(TEXT("Cut boundary segment count"), Boundary->Segments.Num(), 2);
	TestEqual(TEXT("Cut boundary second segment re-based to 250"), Boundary->Segments[1].Dist, 250.0, 0.01);
	TestTrue(TEXT("Cut boundary second segment still solid"), Boundary->Segments[1].bSolidMarking);
	return true;
}

// Verifies SerializeGraph/DeserializeGraph round-trips a boundary/lane graph exactly -- the
// replication scheme the plan calls for: replicate compact flat structs, not live UObjects, and
// reconstruct an identical graph in OnRep. Builds a 2-boundary, 1-lane graph (a single driving
// lane between a left and right edge), serializes it, deserializes into a fresh set of objects,
// and checks both the authored data and the cross-reference wiring survive.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCurveGraphReplicationRoundTripTest, "TerraDyne.RoadCurveGraph.ReplicationRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadCurveGraphReplicationRoundTripTest::RunTest(const FString& Parameters)
{
	URealtimeRoadBoundary* LeftEdge = NewObject<URealtimeRoadBoundary>();
	LeftEdge->LocalOffsets.Add({ 0.0, -175.0, 0.0 });
	LeftEdge->LocalOffsets.Add({ 1000.0, -175.0, 0.0 });
	LeftEdge->RecomputeEffectiveOffsets();
	LeftEdge->Segments.Add({ 0.0, true });

	URealtimeRoadBoundary* RightEdge = NewObject<URealtimeRoadBoundary>();
	RightEdge->LocalOffsets.Add({ 0.0, 175.0, 0.0 });
	RightEdge->LocalOffsets.Add({ 1000.0, 175.0, 0.0 });
	RightEdge->RecomputeEffectiveOffsets();
	RightEdge->Segments.Add({ 0.0, true });

	URealtimeRoadLane* Lane = NewObject<URealtimeRoadLane>();
	Lane->Segments.Add({ 0.0, ERealtimeLaneType::Driving });
	Lane->LeftBoundary = LeftEdge;
	Lane->RightBoundary = RightEdge;
	LeftEdge->RightLane = Lane;
	RightEdge->LeftLane = Lane;

	TArray<URealtimeRoadBoundary*> Boundaries = { LeftEdge, RightEdge };
	TArray<URealtimeRoadLane*> Lanes = { Lane };

	TArray<FReplicatedBoundaryState> BoundaryStates;
	TArray<FReplicatedLaneState> LaneStates;
	RoadCurveGraph::SerializeGraph(Boundaries, Lanes, BoundaryStates, LaneStates);

	TestEqual(TEXT("Serialized boundary count"), BoundaryStates.Num(), 2);
	TestEqual(TEXT("Serialized lane count"), LaneStates.Num(), 1);
	TestEqual(TEXT("Left boundary's right-lane index resolves to the lane"), BoundaryStates[0].RightLaneIndex, 0);
	TestEqual(TEXT("Right boundary's left-lane index resolves to the lane"), BoundaryStates[1].LeftLaneIndex, 0);
	TestEqual(TEXT("Lane's left-boundary index resolves to the left edge"), LaneStates[0].LeftBoundaryIndex, 0);
	TestEqual(TEXT("Lane's right-boundary index resolves to the right edge"), LaneStates[0].RightBoundaryIndex, 1);

	TArray<URealtimeRoadBoundary*> RebuiltBoundaries;
	TArray<URealtimeRoadLane*> RebuiltLanes;
	RoadCurveGraph::DeserializeGraph(GetTransientPackage(), BoundaryStates, LaneStates, RebuiltBoundaries, RebuiltLanes);

	TestEqual(TEXT("Rebuilt boundary count"), RebuiltBoundaries.Num(), 2);
	TestEqual(TEXT("Rebuilt lane count"), RebuiltLanes.Num(), 1);
	TestEqual(TEXT("Rebuilt left edge offset preserved"), RebuiltBoundaries[0]->GetOffset(500.0), -175.0, 0.01);
	TestEqual(TEXT("Rebuilt right edge offset preserved"), RebuiltBoundaries[1]->GetOffset(500.0), 175.0, 0.01);
	TestTrue(TEXT("Rebuilt lane's LeftBoundary points at the rebuilt left edge"), RebuiltLanes[0]->LeftBoundary == RebuiltBoundaries[0]);
	TestTrue(TEXT("Rebuilt lane's RightBoundary points at the rebuilt right edge"), RebuiltLanes[0]->RightBoundary == RebuiltBoundaries[1]);
	TestTrue(TEXT("Rebuilt left edge's RightLane points at the rebuilt lane"), RebuiltBoundaries[0]->RightLane == RebuiltLanes[0]);
	TestTrue(TEXT("Rebuilt right edge's LeftLane points at the rebuilt lane"), RebuiltBoundaries[1]->LeftLane == RebuiltLanes[0]);
	return true;
}
