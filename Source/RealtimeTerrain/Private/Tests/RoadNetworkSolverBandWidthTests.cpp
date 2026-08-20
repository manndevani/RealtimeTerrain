// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadNetworkSolver.h"

// Regression test for the unclamped roadside band width on the solved-junction path: the actor code
// used to assume "the solver already clamps its returns" and hardcode MaxBandWidth to Max() whenever
// SolveIntersection() succeeded, even though SolveNode only ever clamped each corner's *fillet
// radius* against the geometry available there -- a tight/acute corner can still resolve a small
// radius, and a curb+footpath band wider than that radius folds over itself. FNodeSolution now
// exposes that per-corner radius (CornerRadii) so the caller can size the band against the tightest
// one, instead of trusting an unqualified Max().
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverTightCornerRadiusTest, "TerraDyne.RoadNetwork.TightCornerClampsRadius", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadNetworkSolverTightCornerRadiusTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolver;

	// Two ordinary, perpendicular 1000cm arms -- an entirely normal corner, not an exotic edge case.
	// The point is not the angle; it is the deliberately unreasonable requested CornerRadius below,
	// which no arm this short could ever honour, so SolveNode has to clamp it down hard regardless.
	FArm ArmA;
	ArmA.Centreline = { FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0) };
	ArmA.HalfWidth = 300.0;
	ArmA.UserData = 0;

	const double AngleRad = FMath::DegreesToRadians(90.0);
	FArm ArmB;
	ArmB.Centreline = { FVector2D(0.0, 0.0), FVector2D(1000.0 * FMath::Cos(AngleRad), 1000.0 * FMath::Sin(AngleRad)) };
	ArmB.HalfWidth = 300.0;
	ArmB.UserData = 1;

	TArray<FArm> Arms = { ArmA, ArmB };

	FNodeOptions Options;
	Options.CornerRadius = 100000.0; // Deliberately far larger than these short arms can support.
	Options.ArcSegments = 8;

	const FNodeSolution Solution = SolveNode(FVector2D::ZeroVector, Arms, Options);

	TestTrue("Solution should be valid for two well-formed arms", Solution.bValid);
	TestEqual("One CornerRadii entry per arm", Solution.CornerRadii.Num(), Arms.Num());
	if (!Solution.bValid || Solution.CornerRadii.Num() != Arms.Num())
	{
		return false;
	}

	// At least one corner (the acute wedge between the two arms) must have been reported far below
	// the requested radius -- that is the clamp actually happening, not a no-op Max() pass-through.
	double TightestRadius = TNumericLimits<double>::Max();
	for (const double CornerRadius : Solution.CornerRadii)
	{
		TightestRadius = FMath::Min(TightestRadius, CornerRadius);
	}

	AddInfo(FString::Printf(TEXT("Tightest resolved corner radius: %.2f cm (requested %.2f cm)"), TightestRadius, Options.CornerRadius));

	TestTrue("Tightest corner radius should be finite (a real clamp value, not Max())", TightestRadius < TNumericLimits<double>::Max());
	TestTrue("Tightest corner radius should be well below the requested radius", TightestRadius < Options.CornerRadius * 0.5);
	TestTrue("Tightest corner radius should still be positive", TightestRadius > 0.0);

	return true;
}
