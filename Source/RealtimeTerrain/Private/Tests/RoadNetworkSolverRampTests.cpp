// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadNetworkSolver.h"

// Verifies RoadNetworkSolver::IsRampConnection -- the ramp/fork classifier ported from the idea
// behind RoadBuilder's FJunctionGate::IsRampOf -- requires BOTH a shallow angular gap AND a
// driving-lane-count mismatch, matching the real-world signature of a highway on/off-ramp (a
// lane count transition at a shallow merge angle) as distinct from either a perpendicular
// crossing of equal-lane roads or a shallow-angle fork where both branches keep the same lane
// count (a Y split with no actual lane change, which isn't a ramp).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverRampClassificationTest, "TerraDyne.RoadNetwork.RampClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadNetworkSolverRampClassificationTest::RunTest(const FString& Parameters)
{
	const double ShallowAngle = FMath::DegreesToRadians(20.0);
	const double WideAngle = FMath::DegreesToRadians(90.0);

	TestTrue(
		TEXT("Shallow angle + differing lane counts reads as a ramp"),
		RoadNetworkSolver::IsRampConnection(ShallowAngle, 2, 1));
	TestTrue(
		TEXT("Ramp classification is symmetric in which side has fewer lanes"),
		RoadNetworkSolver::IsRampConnection(ShallowAngle, 1, 2));
	TestFalse(
		TEXT("Shallow angle with matching lane counts is a plain fork, not a ramp"),
		RoadNetworkSolver::IsRampConnection(ShallowAngle, 2, 2));
	TestFalse(
		TEXT("Wide angle with differing lane counts is a plain crossing, not a ramp"),
		RoadNetworkSolver::IsRampConnection(WideAngle, 2, 1));
	TestFalse(
		TEXT("Wide angle with matching lane counts is an ordinary crossing"),
		RoadNetworkSolver::IsRampConnection(WideAngle, 2, 2));
	// Right at the boundary should not be a ramp -- the threshold is an open bound (strictly less
	// than), so exactly-40-degrees does not qualify.
	TestFalse(
		TEXT("Exactly the threshold angle does not qualify as a ramp"),
		RoadNetworkSolver::IsRampConnection(RoadNetworkSolver::RampAngleThresholdRadians, 2, 1));
	return true;
}

// Verifies SolveNode actually uses the ramp classification, not just computes it: two arms at the
// same shallow angle resolve to a visibly larger corner radius when their lane counts differ (a
// ramp -- long, gentle merge taper) than when they match (an ordinary shallow fork -- same tight
// kerb-return radius any other corner gets).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverRampCornerRadiusTest, "TerraDyne.RoadNetwork.RampCornerGetsWiderRadius", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadNetworkSolverRampCornerRadiusTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolver;

	auto BuildShallowArms = [](int32 LaneCountA, int32 LaneCountB) -> TArray<FArm>
	{
		FArm ArmA;
		ArmA.Centreline = { FVector2D(0.0, 0.0), FVector2D(5000.0, 0.0) };
		ArmA.HalfWidth = 300.0;
		ArmA.DrivingLaneCount = LaneCountA;
		ArmA.UserData = 0;

		const double AngleRad = FMath::DegreesToRadians(20.0);
		FArm ArmB;
		ArmB.Centreline = { FVector2D(0.0, 0.0), FVector2D(5000.0 * FMath::Cos(AngleRad), 5000.0 * FMath::Sin(AngleRad)) };
		ArmB.HalfWidth = 300.0;
		ArmB.DrivingLaneCount = LaneCountB;
		ArmB.UserData = 1;

		return { ArmA, ArmB };
	};

	FNodeOptions Options;
	Options.CornerRadius = 300.0;
	Options.ArcSegments = 8;
	Options.RampCornerRadiusMultiplier = 3.0;

	TArray<FArm> MatchedLaneArms = BuildShallowArms(2, 2);
	const FNodeSolution NonRampSolution = SolveNode(FVector2D::ZeroVector, MatchedLaneArms, Options);

	TArray<FArm> MismatchedLaneArms = BuildShallowArms(2, 1);
	const FNodeSolution RampSolution = SolveNode(FVector2D::ZeroVector, MismatchedLaneArms, Options);

	TestTrue(TEXT("Non-ramp solution should be valid"), NonRampSolution.bValid);
	TestTrue(TEXT("Ramp solution should be valid"), RampSolution.bValid);
	if (!NonRampSolution.bValid || !RampSolution.bValid) return false;

	// Min, not Max: with only 2 arms, the OTHER "corner" in the loop is the same pair swept the
	// long way around (340 degrees) -- that one never collides and stays at CornerRadii's
	// unconstrained sentinel (DBL_MAX). Max would pick that up instead of the real, finite radius
	// the short-way (20-degree) corner actually resolved.
	double NonRampRadius = TNumericLimits<double>::Max();
	for (const double R : NonRampSolution.CornerRadii) NonRampRadius = FMath::Min(NonRampRadius, R);
	double RampRadius = TNumericLimits<double>::Max();
	for (const double R : RampSolution.CornerRadii) RampRadius = FMath::Min(RampRadius, R);

	AddInfo(FString::Printf(TEXT("Non-ramp corner radius: %.2f cm, ramp corner radius: %.2f cm (requested base %.2f cm)"), NonRampRadius, RampRadius, Options.CornerRadius));

	TestTrue(TEXT("Non-ramp shallow fork keeps close to the base corner radius"), NonRampRadius < Options.CornerRadius * 1.5);
	TestTrue(TEXT("Ramp corner resolves noticeably wider than the base corner radius"), RampRadius > Options.CornerRadius * 1.5);
	return true;
}

// Verifies RoadNetworkSolver::FindGoreApex converges nearer the node for a narrower driving
// half-width than a wider one, at the same angle -- the defining geometric property a gore point
// has to have (a narrower offset from two converging lines meets sooner), and the same
// offset-and-converge technique SolveNode's own corner collision already uses and is tested by
// TerraDyne.RoadNetwork.TightCornerClampsRadius, just applied to the driving edge instead of the
// full surface edge.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverGoreApexTest, "TerraDyne.RoadNetwork.GoreApexNarrowsWithDrivingWidth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadNetworkSolverGoreApexTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolver;

	auto BuildArms = [](double DrivingHalfWidth) -> TArray<FArm>
	{
		FArm ArmA;
		ArmA.Centreline = { FVector2D(0.0, 0.0), FVector2D(5000.0, 0.0) };
		ArmA.DrivingHalfWidth = DrivingHalfWidth;

		const double AngleRad = FMath::DegreesToRadians(20.0);
		FArm ArmB;
		ArmB.Centreline = { FVector2D(0.0, 0.0), FVector2D(5000.0 * FMath::Cos(AngleRad), 5000.0 * FMath::Sin(AngleRad)) };
		ArmB.DrivingHalfWidth = DrivingHalfWidth;

		return { ArmA, ArmB };
	};

	TArray<FArm> NarrowArms = BuildArms(150.0);
	FVector2D NarrowApex;
	const bool bNarrowFound = FindGoreApex(NarrowArms[0], NarrowArms[1], NarrowApex);

	TArray<FArm> WideArms = BuildArms(300.0);
	FVector2D WideApex;
	const bool bWideFound = FindGoreApex(WideArms[0], WideArms[1], WideApex);

	TestTrue(TEXT("Gore apex found for the narrower driving width"), bNarrowFound);
	TestTrue(TEXT("Gore apex found for the wider driving width"), bWideFound);
	if (!bNarrowFound || !bWideFound) return false;

	const double NarrowDistance = NarrowApex.Size();
	const double WideDistance = WideApex.Size();
	AddInfo(FString::Printf(TEXT("Narrow (150cm) apex distance: %.2f cm, wide (300cm) apex distance: %.2f cm"), NarrowDistance, WideDistance));

	TestTrue(TEXT("Both apexes sit ahead of the node, within the sampled arm length"), NarrowDistance > 0.0 && NarrowDistance < 5000.0 && WideDistance > 0.0 && WideDistance < 5000.0);
	TestTrue(TEXT("Narrower driving width converges nearer the node than a wider one"), NarrowDistance < WideDistance);
	return true;
}

// Regression test for a real gap this exact scenario surfaced end-to-end
// (TerraDyne.RoadJunctionActor.RampTopologyDetectsGoreCorner): a corner whose true mitre point
// falls beyond either arm's own sampled length takes SolveNode's separate bevel path rather than
// the arc-fillet one, and that path was not classifying ramps at all -- so a realistic ramp with
// generous curb+footpath width (which is exactly what pushes the mitre point out of range) was
// silently invisible to ramp/gore logic, not an exotic edge case. HalfWidth here (585) matches
// CrossSectionSettings' real defaults (400 base + 25 curb + 160 footpath), and the arm length
// (3000) matches what the integration test actually used.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverBevelPathRampTest, "TerraDyne.RoadNetwork.BevelPathStillClassifiesRamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadNetworkSolverBevelPathRampTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolver;

	auto BuildArms = [](int32 LaneCountA, int32 LaneCountB) -> TArray<FArm>
	{
		FArm ArmA;
		ArmA.Centreline = { FVector2D(0.0, 0.0), FVector2D(3000.0, 0.0) };
		ArmA.HalfWidth = 585.0;
		ArmA.DrivingLaneCount = LaneCountA;

		const double AngleRad = FMath::DegreesToRadians(20.0);
		FArm ArmB;
		ArmB.Centreline = { FVector2D(0.0, 0.0), FVector2D(3000.0 * FMath::Cos(AngleRad), 3000.0 * FMath::Sin(AngleRad)) };
		ArmB.HalfWidth = 585.0;
		ArmB.DrivingLaneCount = LaneCountB;

		return { ArmA, ArmB };
	};

	FNodeOptions Options;
	Options.CornerRadius = 300.0;

	TArray<FArm> RampArms = BuildArms(2, 1);
	const FNodeSolution RampSolution = SolveNode(FVector2D::ZeroVector, RampArms, Options);
	TestTrue(TEXT("Ramp solution should be valid"), RampSolution.bValid);
	if (!RampSolution.bValid) return false;

	// Confirms this really did take the bevel path (radius forced to exactly 0), not the arc path
	// -- otherwise this test would not actually be exercising the branch it's named for.
	TestEqual(TEXT("Corner resolved via the bevel path (radius 0), confirming this exercises that branch"), RampSolution.CornerRadii[0], 0.0, 0.001);
	TestTrue(TEXT("Bevel-path corner is still classified as a ramp"), RampSolution.IsRampCorner[0]);

	TArray<FArm> NonRampArms = BuildArms(2, 2);
	const FNodeSolution NonRampSolution = SolveNode(FVector2D::ZeroVector, NonRampArms, Options);
	TestTrue(TEXT("Non-ramp solution should be valid"), NonRampSolution.bValid);
	if (!NonRampSolution.bValid) return false;
	TestFalse(TEXT("Bevel-path corner with matching lane counts is not classified as a ramp"), NonRampSolution.IsRampCorner[0]);
	return true;
}

// Verifies RoadNetworkSolver::MapLaneConnections -- the foundational per-lane connectivity
// decision real connector geometry and lane-marking continuity both build on. A through
// connection (matching or growing lane count) maps every source lane 1:1; a merge (fewer lanes on
// the far side) leaves the excess near-corner-side lanes unmapped, since those are exactly the
// ones that end at the gore point instead of crossing the corner.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverLaneConnectionMapTest, "TerraDyne.RoadNetwork.LaneConnectionMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadNetworkSolverLaneConnectionMapTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolver;

	{
		// Equal lane counts: a plain through connection, every lane maps 1:1.
		TArray<int32> Map;
		MapLaneConnections(2, 2, Map);
		TestEqual(TEXT("Equal-count map size"), Map.Num(), 2);
		TestEqual(TEXT("Equal-count lane 0 maps to lane 0"), Map[0], 0);
		TestEqual(TEXT("Equal-count lane 1 maps to lane 1"), Map[1], 1);
	}
	{
		// A 2-lane road merging down to a 1-lane ramp: only the near-corner lane (index 0)
		// connects; the far lane has nothing on the ramp side and is unmapped.
		TArray<int32> Map;
		MapLaneConnections(2, 1, Map);
		TestEqual(TEXT("Merge-down map size matches the wider side's lane count"), Map.Num(), 2);
		TestEqual(TEXT("Merge-down near-corner lane 0 still connects"), Map[0], 0);
		TestEqual(TEXT("Merge-down far lane 1 is unmapped (ends at the gore)"), Map[1], INDEX_NONE);
	}
	{
		// A 1-lane ramp joining a 2-lane road: the ramp's only lane connects to the main road's
		// near-corner lane.
		TArray<int32> Map;
		MapLaneConnections(1, 2, Map);
		TestEqual(TEXT("Merge-up map size matches the narrower side's lane count"), Map.Num(), 1);
		TestEqual(TEXT("Merge-up lane 0 connects to the near-corner lane"), Map[0], 0);
	}
	{
		// Zero lanes on either side is a degenerate but real input (an unstyled/undefined arm) --
		// must not crash and must produce an empty map rather than garbage.
		TArray<int32> Map;
		MapLaneConnections(0, 2, Map);
		TestEqual(TEXT("Zero source lanes produces an empty map"), Map.Num(), 0);
	}
	return true;
}

// Verifies RoadNetworkSolver::BuildLaneConnectorCentreline's endpoint positions and tangent
// directions against an independent, hand-derived expectation for a simple 90-degree corner: a
// vehicle approaching ArmA's cap moves TOWARD the node (opposite ArmA's own Centreline direction,
// which points away from it by definition), then continues past ArmB's cap moving AWAY from the
// node and on into ArmB's own body (matching ArmB's Centreline direction exactly) -- the tangent
// convention that keeps the connector from kinking at either cap. Checked directly rather than
// just "did it run", since a sign error here would still produce a plausible-looking curve.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverLaneConnectorCentrelineTest, "TerraDyne.RoadNetwork.LaneConnectorCentreline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadNetworkSolverLaneConnectorCentrelineTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolver;

	// ArmA heads +X away from the node; its cap (TrimDistance=200) sits at (200,0).
	FArm ArmA;
	ArmA.Centreline = { FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0) };
	ArmA.DrivingHalfWidth = 400.0;
	ArmA.DrivingLaneCount = 2;
	ArmA.TrimDistance = 200.0;

	// ArmB heads +Y away from the node; its cap sits at (0,200).
	FArm ArmB;
	ArmB.Centreline = { FVector2D(0.0, 0.0), FVector2D(0.0, 1000.0) };
	ArmB.DrivingHalfWidth = 400.0;
	ArmB.DrivingLaneCount = 2;
	ArmB.TrimDistance = 200.0;

	// Lane 1 (the far lane, not the degenerate near-corner lane 0 where both arms' edges coincide
	// right at the corner) on each side: 400*2/2=400cm lane width, so lane 1's centre sits at
	// DrivingHalfWidth - 400*(1+0.5) = -200 from ArmA's own centreline, and mirrored for ArmB.
	TArray<FVector2D> Centreline;
	const bool bBuilt = BuildLaneConnectorCentreline(ArmA, 1, ArmB, 1, 8, Centreline);
	TestTrue(TEXT("Connector should build for valid lane indices"), bBuilt);
	if (!bBuilt) return false;

	TestEqual(TEXT("Centreline has NumSegments+1 points"), Centreline.Num(), 9);

	const FVector2D ExpectedStart(200.0, -200.0);
	const FVector2D ExpectedEnd(-200.0, 200.0);
	TestTrue(TEXT("Start position matches the hand-derived lane-1 offset on ArmA"), Centreline[0].Equals(ExpectedStart, 0.01));
	TestTrue(TEXT("End position matches the hand-derived lane-1 offset on ArmB"), Centreline.Last().Equals(ExpectedEnd, 0.01));

	// Tangent at each end, from a short finite difference against the neighbouring point.
	const FVector2D StartTangent = (Centreline[1] - Centreline[0]).GetSafeNormal();
	const FVector2D EndTangent = (Centreline.Last() - Centreline[Centreline.Num() - 2]).GetSafeNormal();
	AddInfo(FString::Printf(TEXT("Start tangent: (%.3f, %.3f), end tangent: (%.3f, %.3f)"), StartTangent.X, StartTangent.Y, EndTangent.X, EndTangent.Y));

	// Start: moving toward the node, i.e. -X (opposite ArmA's own +X Centreline direction).
	TestTrue(TEXT("Start tangent points toward the node (-X), not away from it"), StartTangent.X < -0.5);
	// End: continuing away from the node into ArmB's body, i.e. +Y (matching ArmB's own direction).
	TestTrue(TEXT("End tangent points away from the node into ArmB (+Y)"), EndTangent.Y > 0.5);

	for (const FVector2D& Point : Centreline)
	{
		TestFalse(TEXT("No NaN in the connector centreline"), FMath::IsNaN(Point.X) || FMath::IsNaN(Point.Y));
	}

	TestFalse(TEXT("Out-of-range lane index on ArmA fails cleanly"), BuildLaneConnectorCentreline(ArmA, 5, ArmB, 0, 8, Centreline));
	TestFalse(TEXT("Zero segments fails cleanly"), BuildLaneConnectorCentreline(ArmA, 0, ArmB, 0, 0, Centreline));
	return true;
}
