// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadSpiral.h"

// Verifies GetSpiralPos/GetSpiralRadian against exact closed forms for the two degenerate
// (dCurv == 0) branches, which don't touch the ported Fresnel-integral numerics at all: a straight
// line (initCurv == 0) and a circular arc (initCurv != 0, constant curvature). Getting these two
// closed forms right is a precondition for trusting the general clothoid branch tested separately
// below.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSpiralClosedFormTest, "TerraDyne.RoadSpiral.StraightAndArcClosedForm", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSpiralClosedFormTest::RunTest(const FString& Parameters)
{
	{
		// Straight line: curvature 0 throughout, so position is just Length along InitTheta and
		// heading never changes.
		const double Theta = 0.3;
		const double Length = 500.0;
		const FVector2D Pos = RoadSpiral::GetSpiralPos(0.0, 0.0, Theta, 0.0, 0.0, Length);
		TestEqual(TEXT("Straight line X"), Pos.X, Length * FMath::Cos(Theta), 0.001);
		TestEqual(TEXT("Straight line Y"), Pos.Y, Length * FMath::Sin(Theta), 0.001);
		TestEqual(TEXT("Straight line heading is unchanged"), RoadSpiral::GetSpiralRadian(Theta, 0.0, 0.0, Length), Theta, 1.0e-9);
	}
	{
		// Circular arc: constant curvature K traces a circle of radius 1/K centred at
		// (InitX,InitY) + R * (-sin(InitTheta), cos(InitTheta)) -- with InitTheta == 0 that's simply
		// (0, R). Heading should advance linearly with arc length (K * S), and every sampled point
		// must sit exactly R from that centre.
		const double K = 1.0 / 200.0;
		const double R = 1.0 / K;
		const FVector2D Centre(0.0, R);
		for (const double S : { 50.0, 150.0, 314.159, 600.0 })
		{
			const FVector2D Pos = RoadSpiral::GetSpiralPos(0.0, 0.0, 0.0, K, 0.0, S);
			const double DistFromCentre = FVector2D::Distance(Pos, Centre);
			AddInfo(FString::Printf(TEXT("S=%.1f pos=(%.3f,%.3f) distFromCentre=%.4f"), S, Pos.X, Pos.Y, DistFromCentre));
			TestEqual(TEXT("Arc point stays exactly R from the circle's centre"), DistFromCentre, R, 0.01);

			const double Heading = RoadSpiral::GetSpiralRadian(0.0, K, 0.0, S);
			TestEqual(TEXT("Arc heading advances linearly (K * S)"), Heading, K * S, 1.0e-9);
		}
	}
	return true;
}

// Verifies a genuine clothoid (dCurv != 0, so GetSpiralPos takes the Fresnel-integral branch) two
// ways: first, that ComputeSpiralLength's algebraic inverse actually round-trips through
// GetSpiralRadian (an exact identity: L = (Theta1-Theta0)/((K1-K0)/2+K0) is constructed precisely so
// GetSpiralRadian(Theta0, K0, (K1-K0)/L, L) reproduces Theta1); second, and more importantly, that
// GetSpiralPos's Fresnel-integral position actually integrates the heading GetSpiralRadian reports
// -- checked via a central-difference numeric tangent against (cos(heading), sin(heading)), which
// would catch a wrong Fresnel coefficient or sign error that a closed-form check alone could not
// (the straight-line/arc cases above never exercise this branch at all).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSpiralClothoidConsistencyTest, "TerraDyne.RoadSpiral.ClothoidConsistency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSpiralClothoidConsistencyTest::RunTest(const FString& Parameters)
{
	{
		const double Theta0 = 0.0;
		const double Theta1 = 0.4;
		const double K0 = 0.0;
		const double K1 = 1.0 / 300.0;
		const double Length = RoadSpiral::ComputeSpiralLength(Theta0, Theta1, K0, K1);
		TestTrue(TEXT("ComputeSpiralLength returns a positive length for a forward-turning target"), Length > 0.0);

		const double G = (K1 - K0) / Length;
		const double RoundTrippedTheta = RoadSpiral::GetSpiralRadian(Theta0, K0, G, Length);
		TestEqual(TEXT("GetSpiralRadian round-trips ComputeSpiralLength's target heading"), RoundTrippedTheta, Theta1, 1.0e-9);
	}
	{
		// A genuine clothoid: curvature ramps from 0 at S=0 to a real value over Length, InitTheta
		// deliberately non-zero so a sign error in the Fresnel rotation-by-theta step wouldn't hide
		// behind InitTheta == 0's identity rotation.
		const double InitTheta = 0.2;
		const double InitCurv = 0.0;
		const double DCurv = 1.0 / (300.0 * 800.0); // Curvature reaches 1/300 by S=800.
		const double Length = 800.0;
		const double H = 1.0; // Small step relative to Length for a central-difference tangent.

		for (const double S : { 100.0, 400.0, 700.0 })
		{
			const FVector2D PosMinus = RoadSpiral::GetSpiralPos(0.0, 0.0, InitTheta, InitCurv, DCurv, S - H);
			const FVector2D PosPlus = RoadSpiral::GetSpiralPos(0.0, 0.0, InitTheta, InitCurv, DCurv, S + H);
			FVector2D NumericTangent = (PosPlus - PosMinus) / (2.0 * H);
			NumericTangent.Normalize();

			const double Heading = RoadSpiral::GetSpiralRadian(InitTheta, InitCurv, DCurv, S);
			const FVector2D ExpectedTangent(FMath::Cos(Heading), FMath::Sin(Heading));

			AddInfo(FString::Printf(TEXT("S=%.1f numericTangent=(%.5f,%.5f) expectedTangent=(%.5f,%.5f)"),
				S, NumericTangent.X, NumericTangent.Y, ExpectedTangent.X, ExpectedTangent.Y));
			TestEqual(TEXT("Clothoid position's numeric tangent matches GetSpiralRadian's heading (X)"), NumericTangent.X, ExpectedTangent.X, 0.001);
			TestEqual(TEXT("Clothoid position's numeric tangent matches GetSpiralRadian's heading (Y)"), NumericTangent.Y, ExpectedTangent.Y, 0.001);
		}
	}
	return true;
}

// Verifies RoadSpiral::BuildSpiralCentreline's actual geometric contract on a 90-degree corner: the
// fitted path departs the corner heading in the same direction as the incoming arm, arrives heading
// in the same direction as the outgoing arm, never jumps heading abruptly between consecutive
// samples (the whole point of a spiral-fit corner over a raw polyline corner -- curvature
// continuity), and is never longer than the sharp unrounded polyline it replaces (rounding a corner
// can only cut distance, never add it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSpiralCentrelineCornerTest, "TerraDyne.RoadSpiral.CentrelineCornerIsContinuous", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSpiralCentrelineCornerTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> ControlPoints = { FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0), FVector2D(1000.0, 1000.0) };
	const double MaxRadius = 500.0;
	const double CurvatureBlend = 0.5;
	const double SampleStep = 25.0;

	TArray<FVector2D> Points;
	TArray<double> Distances;
	RoadSpiral::BuildSpiralCentreline(ControlPoints, MaxRadius, CurvatureBlend, SampleStep, Points, Distances);

	TestTrue(TEXT("Centreline produced sample points"), Points.Num() > 10);
	if (Points.Num() <= 10) return false;

	// Heading near the very start should match the incoming arm (+X, 0 radians); heading near the
	// very end should match the outgoing arm (+Y, PI/2 radians).
	const FVector2D StartDir = (Points[1] - Points[0]).GetSafeNormal();
	const FVector2D EndDir = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
	AddInfo(FString::Printf(TEXT("StartDir=(%.4f,%.4f) EndDir=(%.4f,%.4f) totalLength=%.2f"), StartDir.X, StartDir.Y, EndDir.X, EndDir.Y, Distances.Last()));
	TestEqual(TEXT("Path departs along the incoming arm's direction"), StartDir.X, 1.0, 0.01);
	TestEqual(TEXT("Path departs along the incoming arm's direction (Y)"), StartDir.Y, 0.0, 0.01);
	TestEqual(TEXT("Path arrives along the outgoing arm's direction"), EndDir.X, 0.0, 0.01);
	TestEqual(TEXT("Path arrives along the outgoing arm's direction (Y)"), EndDir.Y, 1.0, 0.01);

	// Curvature continuity: the heading between consecutive sample segments should never jump by
	// more than a small tolerance -- a raw unrounded polyline corner would show a single ~90 degree
	// jump right at the corner; a spiral-fit corner spreads that turn smoothly across many samples.
	double MaxHeadingJump = 0.0;
	double PrevHeading = FMath::Atan2(StartDir.Y, StartDir.X);
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		const FVector2D SegDir = (Points[i] - Points[i - 1]);
		if (SegDir.IsNearlyZero()) continue;
		const double Heading = FMath::Atan2(SegDir.Y, SegDir.X);
		const double Jump = FMath::Abs(FMath::FindDeltaAngleRadians(PrevHeading, Heading));
		MaxHeadingJump = FMath::Max(MaxHeadingJump, Jump);
		PrevHeading = Heading;
	}
	AddInfo(FString::Printf(TEXT("Max heading jump between consecutive samples: %.4f radians"), MaxHeadingJump));
	TestTrue(TEXT("No single sample-to-sample heading jump exceeds a few degrees (curvature continuity)"), MaxHeadingJump < 0.1);

	// Rounding a corner can only cut distance relative to the sharp two-segment polyline it
	// replaces (1000 + 1000 = 2000cm here), never exceed it.
	TestTrue(TEXT("Fitted path is not longer than the sharp unrounded corner"), Distances.Last() <= 2000.0 + 0.1);
	TestTrue(TEXT("Fitted path is meaningfully shorter than the sharp corner (it actually rounds it)"), Distances.Last() < 1999.0);

	return true;
}
