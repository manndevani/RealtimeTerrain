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
