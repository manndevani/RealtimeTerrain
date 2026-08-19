// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadNetworkSolver.h"

namespace RoadNetworkSolver
{
namespace
{
	/** Points nearer than this are the same point. Guards the polygon against zero-length edges. */
	constexpr double WeldTolerance = 10.0;

	FORCEINLINE double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** Rotates 90 degrees counter-clockwise, giving the left normal of a direction of travel. */
	FORCEINLINE FVector2D LeftNormal(const FVector2D& Direction)
	{
		return FVector2D(-Direction.Y, Direction.X);
	}

	/**
	 * Crossing of two segments. Returns false when parallel or when the crossing falls outside
	 * either segment.
	 */
	bool SegmentIntersect(
		const FVector2D& A0, const FVector2D& A1,
		const FVector2D& B0, const FVector2D& B1,
		FVector2D& OutPoint, double& OutTA, double& OutTB)
	{
		const FVector2D DirA = A1 - A0;
		const FVector2D DirB = B1 - B0;

		const double Denominator = Cross2D(DirA, DirB);
		if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector2D Delta = B0 - A0;
		OutTA = Cross2D(Delta, DirB) / Denominator;
		OutTB = Cross2D(Delta, DirA) / Denominator;

		if (OutTA < 0.0 || OutTA > 1.0 || OutTB < 0.0 || OutTB > 1.0)
		{
			return false;
		}

		OutPoint = A0 + DirA * OutTA;
		return true;
	}

	/** Drops consecutive duplicates, which otherwise produce zero-length segments and NaN normals. */
	void WeldPolyline(TArray<FVector2D>& Points)
	{
		for (int32 i = Points.Num() - 1; i > 0; --i)
		{
			if (FVector2D::DistSquared(Points[i], Points[i - 1]) < WeldTolerance * WeldTolerance)
			{
				Points.RemoveAt(i);
			}
		}
	}
}

double PolylineLength(const TArray<FVector2D>& Polyline)
{
	double Total = 0.0;
	for (int32 i = 1; i < Polyline.Num(); ++i)
	{
		Total += FVector2D::Distance(Polyline[i - 1], Polyline[i]);
	}
	return Total;
}

FVector2D PointAtDistance(const TArray<FVector2D>& Polyline, double Distance)
{
	if (Polyline.Num() == 0) return FVector2D::ZeroVector;
	if (Polyline.Num() == 1 || Distance <= 0.0) return Polyline[0];

	double Travelled = 0.0;
	for (int32 i = 1; i < Polyline.Num(); ++i)
	{
		const double SegmentLength = FVector2D::Distance(Polyline[i - 1], Polyline[i]);
		if (Travelled + SegmentLength >= Distance)
		{
			const double Alpha = (SegmentLength > UE_DOUBLE_SMALL_NUMBER)
				? (Distance - Travelled) / SegmentLength
				: 0.0;
			return FMath::Lerp(Polyline[i - 1], Polyline[i], Alpha);
		}
		Travelled += SegmentLength;
	}
	return Polyline.Last();
}

FVector2D DirectionAtDistance(const TArray<FVector2D>& Polyline, double Distance)
{
	if (Polyline.Num() < 2) return FVector2D(1.0, 0.0);

	double Travelled = 0.0;
	for (int32 i = 1; i < Polyline.Num(); ++i)
	{
		const double SegmentLength = FVector2D::Distance(Polyline[i - 1], Polyline[i]);
		if (Travelled + SegmentLength >= Distance || i == Polyline.Num() - 1)
		{
			const FVector2D Direction = (Polyline[i] - Polyline[i - 1]).GetSafeNormal();
			return Direction.IsNearlyZero() ? FVector2D(1.0, 0.0) : Direction;
		}
		Travelled += SegmentLength;
	}
	return FVector2D(1.0, 0.0);
}

double ProjectOntoPolyline(const TArray<FVector2D>& Polyline, const FVector2D& Query)
{
	double BestDistanceAlong = 0.0;
	double BestDistanceSq = TNumericLimits<double>::Max();
	double Travelled = 0.0;

	for (int32 i = 1; i < Polyline.Num(); ++i)
	{
		const FVector2D Start = Polyline[i - 1];
		const FVector2D Segment = Polyline[i] - Start;
		const double SegmentLengthSq = Segment.SizeSquared();

		double Alpha = 0.0;
		if (SegmentLengthSq > UE_DOUBLE_SMALL_NUMBER)
		{
			Alpha = FMath::Clamp(FVector2D::DotProduct(Query - Start, Segment) / SegmentLengthSq, 0.0, 1.0);
		}

		const FVector2D Closest = Start + Segment * Alpha;
		const double DistanceSq = FVector2D::DistSquared(Closest, Query);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestDistanceAlong = Travelled + FMath::Sqrt(SegmentLengthSq) * Alpha;
		}

		Travelled += FMath::Sqrt(SegmentLengthSq);
	}

	return BestDistanceAlong;
}

void OffsetPolyline(const TArray<FVector2D>& In, double Distance, TArray<FVector2D>& Out, double MaxMiterScale)
{
	Out.Reset();

	TArray<FVector2D> Clean = In;
	WeldPolyline(Clean);
	if (Clean.Num() < 2)
	{
		return;
	}

	const double MaxMiterLength = FMath::Abs(Distance) * FMath::Max(1.0, MaxMiterScale);
	Out.Reserve(Clean.Num() + 4);

	for (int32 i = 0; i < Clean.Num(); ++i)
	{
		const bool bFirst = (i == 0);
		const bool bLast = (i == Clean.Num() - 1);

		const FVector2D Incoming = bFirst
			? (Clean[1] - Clean[0]).GetSafeNormal()
			: (Clean[i] - Clean[i - 1]).GetSafeNormal();

		const FVector2D Outgoing = bLast
			? (Clean[i] - Clean[i - 1]).GetSafeNormal()
			: (Clean[i + 1] - Clean[i]).GetSafeNormal();

		const FVector2D NormalIn = LeftNormal(Incoming);
		const FVector2D NormalOut = LeftNormal(Outgoing);

		if (bFirst || bLast)
		{
			Out.Add(Clean[i] + NormalOut * Distance);
			continue;
		}

		// Mitre: the bisector scaled so the offset edges actually meet. As the corner sharpens the
		// scale diverges, so it is capped and the corner becomes a bevel instead of a spike that
		// would collide with completely unrelated geometry.
		FVector2D Bisector = NormalIn + NormalOut;
		if (Bisector.IsNearlyZero())
		{
			Out.Add(Clean[i] + NormalOut * Distance);
			continue;
		}
		Bisector = Bisector.GetSafeNormal();

		const double CosHalf = FVector2D::DotProduct(Bisector, NormalOut);
		if (FMath::Abs(CosHalf) < UE_DOUBLE_SMALL_NUMBER)
		{
			Out.Add(Clean[i] + NormalOut * Distance);
			continue;
		}

		const double MiterLength = Distance / CosHalf;
		if (FMath::Abs(MiterLength) > MaxMiterLength)
		{
			// Bevel: both offset edge ends, so the corner is cut off rather than extended.
			Out.Add(Clean[i] + NormalIn * Distance);
			Out.Add(Clean[i] + NormalOut * Distance);
		}
		else
		{
			Out.Add(Clean[i] + Bisector * MiterLength);
		}
	}

	WeldPolyline(Out);
}

bool IntersectPolylines(
	const TArray<FVector2D>& A,
	const TArray<FVector2D>& B,
	FVector2D& OutPoint,
	double& OutDistanceAlongA,
	double& OutDistanceAlongB)
{
	bool bFound = false;
	double BestAlongA = -1.0;

	double TravelledA = 0.0;
	for (int32 i = 1; i < A.Num(); ++i)
	{
		const double SegmentLengthA = FVector2D::Distance(A[i - 1], A[i]);

		double TravelledB = 0.0;
		for (int32 j = 1; j < B.Num(); ++j)
		{
			const double SegmentLengthB = FVector2D::Distance(B[j - 1], B[j]);

			FVector2D Point;
			double TA = 0.0;
			double TB = 0.0;
			if (SegmentIntersect(A[i - 1], A[i], B[j - 1], B[j], Point, TA, TB))
			{
				const double AlongA = TravelledA + SegmentLengthA * TA;

				// Furthest along A wins: that is the collision that forces the largest trim, and
				// anything nearer would leave the two surfaces still overlapping.
				if (AlongA > BestAlongA)
				{
					BestAlongA = AlongA;
					OutPoint = Point;
					OutDistanceAlongA = AlongA;
					OutDistanceAlongB = TravelledB + SegmentLengthB * TB;
					bFound = true;
				}
			}

			TravelledB += SegmentLengthB;
		}

		TravelledA += SegmentLengthA;
	}

	return bFound;
}

FNodeSolution SolveNode(const FVector2D& NodeLocation, TArray<FArm>& Arms)
{
	FNodeSolution Solution;

	// Strip arms that cannot contribute a direction.
	Arms.RemoveAll([](const FArm& Arm)
	{
		return Arm.Centreline.Num() < 2 || PolylineLength(Arm.Centreline) < WeldTolerance;
	});

	if (Arms.Num() < 2)
	{
		return Solution;
	}

	// Sort counter-clockwise about the node. Every downstream step — which sides face each other,
	// which arms are adjacent, the winding of the final boundary — depends on this order.
	for (FArm& Arm : Arms)
	{
		const FVector2D Direction = DirectionAtDistance(Arm.Centreline, 0.0);
		Arm.AngleRad = FMath::Atan2(Direction.Y, Direction.X);
		Arm.TrimDistance = 0.0;
	}
	Arms.Sort([](const FArm& A, const FArm& B) { return A.AngleRad < B.AngleRad; });

	const int32 NumArms = Arms.Num();

	// Both offset sides of every arm, once.
	TArray<TArray<FVector2D>> LeftSides;
	TArray<TArray<FVector2D>> RightSides;
	LeftSides.SetNum(NumArms);
	RightSides.SetNum(NumArms);

	for (int32 i = 0; i < NumArms; ++i)
	{
		OffsetPolyline(Arms[i].Centreline, Arms[i].HalfWidth, LeftSides[i]);
		OffsetPolyline(Arms[i].Centreline, -Arms[i].HalfWidth, RightSides[i]);
	}

	// Collision per adjacent pair. Going counter-clockwise, arm i's left side and arm i+1's right
	// side are the two edges bounding the wedge between them, so those are the ones that collide.
	TArray<FVector2D> CollisionPoints;
	CollisionPoints.SetNum(NumArms);
	TArray<bool> HasCollision;
	HasCollision.Init(false, NumArms);

	// Tracked per arm, not per corner: an arm is constrained if either of its two neighbours
	// collided with it.
	TArray<bool> ArmConstrained;
	ArmConstrained.Init(false, NumArms);

	for (int32 i = 0; i < NumArms; ++i)
	{
		const int32 Next = (i + 1) % NumArms;

		FVector2D Point;
		double AlongLeft = 0.0;
		double AlongRight = 0.0;
		if (!IntersectPolylines(LeftSides[i], RightSides[Next], Point, AlongLeft, AlongRight))
		{
			continue;
		}

		CollisionPoints[i] = Point;
		HasCollision[i] = true;
		ArmConstrained[i] = true;
		ArmConstrained[Next] = true;

		// The collision is on the offset sides; the trim has to be expressed on the centreline, so
		// project it back. Each arm trims to the furthest demand made of it by either neighbour.
		Arms[i].TrimDistance = FMath::Max(
			Arms[i].TrimDistance, ProjectOntoPolyline(Arms[i].Centreline, Point));
		Arms[Next].TrimDistance = FMath::Max(
			Arms[Next].TrimDistance, ProjectOntoPolyline(Arms[Next].Centreline, Point));
	}

	// An arm that no neighbour collided with — two opposite arms, or a lone stub — is otherwise
	// unconstrained and would be trimmed to nothing, leaving a sliver of surface. Half the widest
	// arm is the smallest stand-off that still spans the node.
	//
	// This floor applies ONLY to unconstrained arms. Applying it to every arm overrode the
	// collision results and made a wide road meeting a narrow one trim as if both were wide.
	double WidestHalf = 0.0;
	for (const FArm& Arm : Arms)
	{
		WidestHalf = FMath::Max(WidestHalf, Arm.HalfWidth);
	}

	for (int32 i = 0; i < NumArms; ++i)
	{
		const double Available = PolylineLength(Arms[i].Centreline);
		double Trim = Arms[i].TrimDistance;

		if (!ArmConstrained[i])
		{
			Trim = FMath::Max(Trim, WidestHalf);
		}

		Arms[i].TrimDistance = FMath::Clamp(Trim, 0.0, Available * 0.9);
	}

	// Boundary: each arm contributes its trimmed end projected to both sides, and each corner
	// contributes its collision point. Including the collisions is what stops the polygon
	// ballooning out past the corners.
	struct FBoundaryPoint
	{
		FVector2D Position;
		double Angle;
	};

	TArray<FBoundaryPoint> Points;
	Points.Reserve(NumArms * 3);

	auto AddPoint = [&Points, &NodeLocation](const FVector2D& Position)
	{
		const FVector2D Offset = Position - NodeLocation;
		if (Offset.IsNearlyZero())
		{
			return;
		}
		Points.Add({ Position, FMath::Atan2(Offset.Y, Offset.X) });
	};

	for (int32 i = 0; i < NumArms; ++i)
	{
		const FVector2D End = PointAtDistance(Arms[i].Centreline, Arms[i].TrimDistance);
		const FVector2D Direction = DirectionAtDistance(Arms[i].Centreline, Arms[i].TrimDistance);
		const FVector2D Normal = LeftNormal(Direction);

		AddPoint(End - Normal * Arms[i].HalfWidth);
		AddPoint(End + Normal * Arms[i].HalfWidth);

		if (HasCollision[i])
		{
			AddPoint(CollisionPoints[i]);
		}
	}

	if (Points.Num() < 3)
	{
		return Solution;
	}

	// Sorting boundary points by angle about the node is the step A/B Street flags as fragile: it
	// degrades when arms curve sharply near the node and can produce a bowtie. It holds for the
	// straight-ish approaches roads are drawn with here, and the weld below removes the
	// near-duplicates that would otherwise make a self-intersection certain.
	Points.Sort([](const FBoundaryPoint& A, const FBoundaryPoint& B) { return A.Angle < B.Angle; });

	Solution.Boundary.Reserve(Points.Num());
	for (const FBoundaryPoint& Point : Points)
	{
		Solution.Boundary.Add(Point.Position);
	}

	WeldPolyline(Solution.Boundary);
	if (Solution.Boundary.Num() >= 2 &&
		FVector2D::DistSquared(Solution.Boundary[0], Solution.Boundary.Last()) < WeldTolerance * WeldTolerance)
	{
		Solution.Boundary.Pop();
	}

	Solution.bValid = Solution.Boundary.Num() >= 3;
	return Solution;
}
}
