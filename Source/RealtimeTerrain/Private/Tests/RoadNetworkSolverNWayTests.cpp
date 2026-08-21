// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadNetworkSolver.h"

// Battery test answering a concrete question: does SolveNode's algorithm genuinely generalise to an
// arbitrary number of arms, or does it only look that way because the loops are written with modular
// indexing while some other part of the geometry secretly assumes 3 or 4? Exercises N from 2 up to 16,
// both evenly spaced (the easy case) and deliberately clustered at acute angles (the case the header's
// own doc comment calls out as where offset-collide-trim methods are known to struggle -- "mitre joins
// run away at very acute angles", "near-duplicate points have to be welded or the polygon
// self-intersects").
//
// A boundary that resolves to something is not enough: this also checks the result is *geometrically
// sane* -- no NaN/Inf, no vertex flung absurdly far from the node (the exact symptom the
// RealtimeRoadJunctionActor.cpp comment about "sprawling flanges" describes), and no self-intersecting
// polygon edge (a torn, bowtied intersection surface).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkSolverNWayBatteryTest, "TerraDyne.RoadNetwork.NWayBattery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace RoadNetworkSolverNWayTest
{
	using namespace RoadNetworkSolver;

	// Two finite segments' proper intersection test (ignores shared endpoints -- adjacent boundary
	// edges legitimately share a vertex, that is not the self-intersection this is looking for).
	bool SegmentsProperlyCross(const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1)
	{
		auto Cross = [](const FVector2D& O, const FVector2D& P, const FVector2D& Q) -> double
		{
			return (P.X - O.X) * (Q.Y - O.Y) - (P.Y - O.Y) * (Q.X - O.X);
		};

		const double D1 = Cross(B0, B1, A0);
		const double D2 = Cross(B0, B1, A1);
		const double D3 = Cross(A0, A1, B0);
		const double D4 = Cross(A0, A1, B1);

		return ((D1 > 0) != (D2 > 0)) && (D1 != 0 && D2 != 0) &&
			((D3 > 0) != (D4 > 0)) && (D3 != 0 && D4 != 0);
	}

	// Builds N arms of length ArmLength/HalfWidth at the given angles (radians), all starting at the
	// origin -- matches how RealtimeRoadJunctionActor.cpp's auto-detect path builds FArm::Centreline:
	// index 0 at the node, running away from it.
	TArray<FArm> BuildArms(const TArray<double>& AnglesRad, double ArmLength, double HalfWidth)
	{
		TArray<FArm> Arms;
		Arms.Reserve(AnglesRad.Num());
		for (int32 i = 0; i < AnglesRad.Num(); ++i)
		{
			FArm Arm;
			Arm.Centreline = {
				FVector2D::ZeroVector,
				FVector2D(ArmLength * FMath::Cos(AnglesRad[i]), ArmLength * FMath::Sin(AnglesRad[i]))
			};
			Arm.HalfWidth = HalfWidth;
			Arm.UserData = i;
			Arms.Add(MoveTemp(Arm));
		}
		return Arms;
	}

	TArray<double> EvenlySpacedAngles(int32 N)
	{
		TArray<double> Angles;
		Angles.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			Angles.Add(2.0 * PI * i / N);
		}
		return Angles;
	}

	// Run one case, log diagnostics, and report pass/fail for it into the parent test via T.
	bool RunCase(FAutomationTestBase& T, const FString& CaseName, TArray<double> AnglesRad, double ArmLength, double HalfWidth)
	{
		TArray<FArm> Arms = BuildArms(AnglesRad, ArmLength, HalfWidth);
		const int32 NumArms = Arms.Num();

		FNodeOptions Options;
		Options.CornerRadius = HalfWidth; // A normal, road-proportioned corner -- not the earlier test's deliberately-impossible one.
		Options.ArcSegments = 8;

		const FNodeSolution Solution = SolveNode(FVector2D::ZeroVector, Arms, Options);

		bool bCasePassed = true;
		auto Check = [&](bool bCondition, const FString& Message)
		{
			if (!bCondition)
			{
				T.AddError(FString::Printf(TEXT("[%s] %s"), *CaseName, *Message));
				bCasePassed = false;
			}
		};

		Check(Solution.bValid, TEXT("SolveNode should report a valid solution"));
		if (!Solution.bValid)
		{
			return false;
		}

		Check(Solution.Boundary.Num() >= NumArms, TEXT("Boundary should have at least one vertex per arm"));
		Check(Solution.CornerRadii.Num() == NumArms, TEXT("One CornerRadii entry per (post-weld) arm count mismatch"));
		Check(Solution.RoadsideChains.Num() == NumArms, TEXT("One RoadsideChains entry per arm"));

		// "Sprawling flange" guard: nothing in the boundary should sit further from the node than a
		// generous multiple of the longest reasonable extent (arm half-width plus corner radius),
		// scaled up to allow for legitimately large miters at very acute angles -- this is a sanity
		// ceiling, not a tight bound.
		const double SaneRadius = (HalfWidth + Options.CornerRadius) * 8.0 + 50.0;
		double MaxDistFromNode = 0.0;
		bool bAllFinite = true;
		for (const FBoundaryVertex& V : Solution.Boundary)
		{
			bAllFinite &= FMath::IsFinite(V.Position.X) && FMath::IsFinite(V.Position.Y);
			MaxDistFromNode = FMath::Max(MaxDistFromNode, V.Position.Length());
		}
		Check(bAllFinite, TEXT("All boundary vertices must be finite (no NaN/Inf)"));
		Check(MaxDistFromNode <= SaneRadius, FString::Printf(
			TEXT("Boundary vertex %.1f cm from node exceeds sane ceiling %.1f cm -- looks like a sprawling flange"),
			MaxDistFromNode, SaneRadius));

		// Self-intersection: no two non-adjacent boundary edges should properly cross.
		const int32 NumVerts = Solution.Boundary.Num();
		bool bSelfIntersects = false;
		for (int32 i = 0; i < NumVerts && !bSelfIntersects; ++i)
		{
			const FVector2D& A0 = Solution.Boundary[i].Position;
			const FVector2D& A1 = Solution.Boundary[(i + 1) % NumVerts].Position;
			for (int32 j = i + 1; j < NumVerts; ++j)
			{
				if (j == i || (j + 1) % NumVerts == i) continue; // adjacent edges share a vertex
				const FVector2D& B0 = Solution.Boundary[j].Position;
				const FVector2D& B1 = Solution.Boundary[(j + 1) % NumVerts].Position;
				if (SegmentsProperlyCross(A0, A1, B0, B1))
				{
					bSelfIntersects = true;
					break;
				}
			}
		}
		Check(!bSelfIntersects, TEXT("Boundary polygon self-intersects (torn/bowtied intersection surface)"));

		// Winding. RoadMesh::AppendPrism takes its boundary as `BoundaryCCW` and emits the top cap in
		// the given vertex order specifically because "Counter-clockwise boundary: this order faces
		// +Z" -- a clockwise boundary silently renders the junction surface inside-out (top face
		// pointing down, so the road is invisible from above). SolveNode sorts arms CCW and walks them
		// in order, but RemoveSelfIntersections REVERSES runs of vertices to uncross the polygon, and
		// a 2-opt reversal is not orientation-preserving in general -- so this has to be asserted, not
		// assumed. Shoelace: positive signed area == counter-clockwise.
		double SignedArea = 0.0;
		for (int32 i = 0; i < NumVerts; ++i)
		{
			const FVector2D& P = Solution.Boundary[i].Position;
			const FVector2D& Q = Solution.Boundary[(i + 1) % NumVerts].Position;
			SignedArea += (P.X * Q.Y) - (Q.X * P.Y);
		}
		SignedArea *= 0.5;
		Check(SignedArea > 0.0, FString::Printf(
			TEXT("Boundary winding must stay counter-clockwise for AppendPrism (+Z facing); signed area is %.1f"),
			SignedArea));

		// Trim sanity: every arm should have been trimmed back by a non-negative amount that still
		// leaves some of the arm remaining (a full-length trim would mean the arm's road vanished).
		double WorstTrimFraction = 0.0;
		for (const FArm& Arm : Arms)
		{
			Check(Arm.TrimDistance >= 0.0, TEXT("Arm trim distance should never be negative"));
			Check(Arm.TrimDistance < ArmLength, TEXT("Arm trim distance should never consume the whole arm"));
			WorstTrimFraction = FMath::Max(WorstTrimFraction, Arm.TrimDistance / ArmLength);
		}

		double TightestCornerRadius = TNumericLimits<double>::Max();
		for (const double R : Solution.CornerRadii)
		{
			TightestCornerRadius = FMath::Min(TightestCornerRadius, R);
		}

		T.AddInfo(FString::Printf(
			TEXT("[%s] arms=%d valid=%d boundaryVerts=%d maxDistFromNode=%.1fcm worstTrimFraction=%.2f signedArea=%.0f tightestCornerRadius=%s selfIntersects=%d -> %s"),
			*CaseName, NumArms, Solution.bValid, Solution.Boundary.Num(), MaxDistFromNode, WorstTrimFraction, SignedArea,
			TightestCornerRadius >= TNumericLimits<double>::Max() ? TEXT("unconstrained") : *FString::Printf(TEXT("%.1fcm"), TightestCornerRadius),
			bSelfIntersects ? 1 : 0,
			bCasePassed ? TEXT("PASS") : TEXT("FAIL")));

		// Raw geometry dump for offline visual inspection (parsed out of the log, not consumed by
		// the test itself): boundary as one polygon, each roadside chain as its own polyline.
		FString BoundaryDump;
		for (const FBoundaryVertex& V : Solution.Boundary)
		{
			BoundaryDump += FString::Printf(TEXT("%.1f,%.1f;"), V.Position.X, V.Position.Y);
		}
		T.AddInfo(FString::Printf(TEXT("DUMP_BOUNDARY [%s] %s"), *CaseName, *BoundaryDump));

		for (int32 ChainIdx = 0; ChainIdx < Solution.RoadsideChains.Num(); ++ChainIdx)
		{
			FString ChainDump;
			for (const FBoundaryVertex& V : Solution.RoadsideChains[ChainIdx])
			{
				ChainDump += FString::Printf(TEXT("%.1f,%.1f;"), V.Position.X, V.Position.Y);
			}
			T.AddInfo(FString::Printf(TEXT("DUMP_CHAIN [%s] %d %s"), *CaseName, ChainIdx, *ChainDump));
		}

		FString ArmDump;
		for (const FArm& Arm : Arms)
		{
			ArmDump += FString::Printf(TEXT("%.2f,%.1f;"), Arm.AngleRad, Arm.HalfWidth);
		}
		T.AddInfo(FString::Printf(TEXT("DUMP_ARMS [%s] %s"), *CaseName, *ArmDump));

		return bCasePassed;
	}
}

bool FRoadNetworkSolverNWayBatteryTest::RunTest(const FString& Parameters)
{
	using namespace RoadNetworkSolverNWayTest;

	const double ArmLength = 2000.0;
	const double HalfWidth = 300.0; // ~600cm (6m) two-lane road, matches the project's usual test scale.

	int32 CasesRun = 0;
	int32 CasesPassed = 0;

	auto RunAndTally = [&](const FString& Name, TArray<double> Angles)
	{
		++CasesRun;
		if (RunCase(*this, Name, MoveTemp(Angles), ArmLength, HalfWidth))
		{
			++CasesPassed;
		}
	};

	// --- Evenly spaced, N = 3 through 16: the "does it scale at all" sweep. ---
	//
	// Starts at 3, not 2: two arms at exactly 180 degrees is the degenerate case checked separately
	// below, and it is deliberately NOT expected to produce a junction surface.
	for (const int32 N : { 3, 4, 5, 6, 8, 10, 12, 16 })
	{
		RunAndTally(FString::Printf(TEXT("Even N=%d"), N), EvenlySpacedAngles(N));
	}

	// Two arms at exactly 180 degrees: a straight road passing through the node, the one case with no
	// corner anywhere to build a fillet around. Neither arm collides with the other (their offset
	// sides are parallel), so both fall to the unconstrained stand-off and the boundary is just the
	// two caps -- a plain rectangular slab bridging the gap.
	//
	// That slab matters and is asserted rather than treated as a degenerate throwaway: a junction
	// actor sitting on an otherwise straight road still CUTS that road on both sides, so a solver
	// that declined to produce a surface here would leave a literal hole in the carriageway. The
	// bridging slab is the correct answer, and it still has to satisfy the same winding contract as
	// every other case or it renders inside-out like any other junction surface.
	{
		TArray<FArm> StraightThrough = BuildArms({ 0.0, UE_DOUBLE_PI }, ArmLength, HalfWidth);

		FNodeOptions StraightOptions;
		StraightOptions.CornerRadius = HalfWidth;
		StraightOptions.ArcSegments = 8;

		const FNodeSolution StraightSolution =
			SolveNode(FVector2D::ZeroVector, StraightThrough, StraightOptions);

		double StraightArea = 0.0;
		const int32 StraightVerts = StraightSolution.Boundary.Num();
		for (int32 i = 0; i < StraightVerts; ++i)
		{
			const FVector2D& P = StraightSolution.Boundary[i].Position;
			const FVector2D& Q = StraightSolution.Boundary[(i + 1) % StraightVerts].Position;
			StraightArea += (P.X * Q.Y) - (Q.X * P.Y);
		}
		StraightArea *= 0.5;

		AddInfo(FString::Printf(
			TEXT("[Straight-through pair (2 arms @180deg)] valid=%d boundaryVerts=%d signedArea=%.0f"),
			StraightSolution.bValid, StraightVerts, StraightArea));

		TestTrue(
			TEXT("A straight-through pair must still bridge the cut with a slab, not leave a hole"),
			StraightSolution.bValid);
		TestTrue(
			TEXT("Straight-through slab needs at least 3 vertices to be a real surface"),
			StraightVerts >= 3);
		TestTrue(
			TEXT("Straight-through slab must be counter-clockwise like every other junction surface"),
			StraightArea > 0.0);
	}

	// --- Realistic irregular junctions: not evenly spaced, still well-formed. ---
	RunAndTally(TEXT("Irregular 5-way (roundabout-ish)"), TArray<double>{
		FMath::DegreesToRadians(0.0), FMath::DegreesToRadians(80.0), FMath::DegreesToRadians(150.0),
		FMath::DegreesToRadians(210.0), FMath::DegreesToRadians(300.0)
	});
	RunAndTally(TEXT("Irregular 7-way"), TArray<double>{
		FMath::DegreesToRadians(0.0), FMath::DegreesToRadians(40.0), FMath::DegreesToRadians(95.0),
		FMath::DegreesToRadians(150.0), FMath::DegreesToRadians(190.0), FMath::DegreesToRadians(260.0),
		FMath::DegreesToRadians(320.0)
	});

	// --- Adversarial: several arms clustered within a narrow acute wedge, at increasing N. This is
	// exactly the case the solver header's own doc comment flags as the method's known weak point. ---
	RunAndTally(TEXT("Clustered acute (4 arms in 30deg wedge + 1 opposite)"), TArray<double>{
		FMath::DegreesToRadians(0.0), FMath::DegreesToRadians(10.0), FMath::DegreesToRadians(20.0),
		FMath::DegreesToRadians(30.0), FMath::DegreesToRadians(180.0)
	});
	RunAndTally(TEXT("Clustered acute (6 arms in 15deg wedge + 1 opposite)"), TArray<double>{
		FMath::DegreesToRadians(0.0), FMath::DegreesToRadians(3.0), FMath::DegreesToRadians(6.0),
		FMath::DegreesToRadians(9.0), FMath::DegreesToRadians(12.0), FMath::DegreesToRadians(15.0),
		FMath::DegreesToRadians(180.0)
	});

	// --- Near-duplicate angles: two arms 0.5 degrees apart, the "near-duplicate points have to be
	// welded or the polygon self-intersects" case the header calls out by name. ---
	RunAndTally(TEXT("Near-duplicate angle pair"), TArray<double>{
		FMath::DegreesToRadians(0.0), FMath::DegreesToRadians(0.5), FMath::DegreesToRadians(120.0),
		FMath::DegreesToRadians(240.0)
	});

	AddInfo(FString::Printf(TEXT("N-way battery: %d/%d cases passed"), CasesPassed, CasesRun));

	return CasesPassed == CasesRun;
}
