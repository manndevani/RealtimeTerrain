// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadNetworkDebugLibrary.h"

#include "RoadNetworkSolver.h"

bool URoadNetworkDebugLibrary::SolveStraightNode(
	const FVector2D& NodeLocation,
	const TArray<float>& HeadingsDegrees,
	const TArray<float>& HalfWidths,
	float ArmLength,
	TArray<FVector2D>& OutBoundary,
	TArray<float>& OutTrimDistances)
{
	OutBoundary.Reset();
	OutTrimDistances.Reset();

	const double Length = FMath::Max(100.0f, ArmLength);

	TArray<RoadNetworkSolver::FArm> Arms;
	Arms.Reserve(HeadingsDegrees.Num());

	for (int32 i = 0; i < HeadingsDegrees.Num(); ++i)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(HeadingsDegrees[i]));
		const FVector2D Direction(FMath::Cos(Radians), FMath::Sin(Radians));

		RoadNetworkSolver::FArm Arm;
		Arm.HalfWidth = HalfWidths.IsValidIndex(i) ? FMath::Max(1.0f, HalfWidths[i]) : 400.0;
		Arm.UserData = i;

		// A straight run away from the node, sampled so the polyline maths has segments to work
		// with rather than a single one.
		for (int32 s = 0; s <= 8; ++s)
		{
			const double D = Length * s / 8.0;
			Arm.Centreline.Add(NodeLocation + Direction * D);
		}

		Arms.Add(MoveTemp(Arm));
	}

	const RoadNetworkSolver::FNodeSolution Solution = RoadNetworkSolver::SolveNode(NodeLocation, Arms);

	OutBoundary.Reserve(Solution.Boundary.Num());
	for (const RoadNetworkSolver::FBoundaryVertex& Vertex : Solution.Boundary)
	{
		OutBoundary.Add(Vertex.Position);
	}

	// Arms come back sorted, so report trims against the caller's original ordering.
	OutTrimDistances.Init(0.0f, HeadingsDegrees.Num());
	for (const RoadNetworkSolver::FArm& Arm : Arms)
	{
		if (OutTrimDistances.IsValidIndex(Arm.UserData))
		{
			OutTrimDistances[Arm.UserData] = static_cast<float>(Arm.TrimDistance);
		}
	}

	return Solution.bValid;
}
