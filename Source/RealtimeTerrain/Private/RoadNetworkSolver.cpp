// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadNetworkSolver.h"

#include "Algo/Reverse.h"

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

	/** Drops consecutive duplicates from a boundary, keeping each vertex's provenance with it. */
	void WeldBoundary(TArray<FBoundaryVertex>& Points)
	{
		for (int32 i = Points.Num() - 1; i > 0; --i)
		{
			if (FVector2D::DistSquared(Points[i].Position, Points[i - 1].Position) < WeldTolerance * WeldTolerance)
			{
				Points.RemoveAt(i);
			}
		}
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

	/** True when two segments cross at an interior point of both (shared endpoints don't count). */
	bool SegmentsProperlyCross(const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1)
	{
		auto Side = [](const FVector2D& O, const FVector2D& P, const FVector2D& Q) -> double
		{
			return Cross2D(P - O, Q - O);
		};

		const double D1 = Side(B0, B1, A0);
		const double D2 = Side(B0, B1, A1);
		const double D3 = Side(A0, A1, B0);
		const double D4 = Side(A0, A1, B1);

		return ((D1 > 0) != (D2 > 0)) && D1 != 0 && D2 != 0 &&
			((D3 > 0) != (D4 > 0)) && D3 != 0 && D4 != 0;
	}

	/**
	 * Forces the boundary counter-clockwise, reversing it in place if it came out clockwise.
	 *
	 * RoadMesh::AppendPrism takes its outline as `BoundaryCCW` and emits the top cap in exactly the
	 * order given -- "Counter-clockwise boundary: this order faces +Z". A clockwise boundary is not
	 * rejected or corrected there; it silently builds the junction slab inside-out, top face pointing
	 * down, so the road surface is invisible from above and only the underside renders.
	 *
	 * Walking arms in CCW angular order does not by itself guarantee a CCW *boundary*: each arm
	 * contributes its cap right-then-left plus a corner chain, and once trims grow large enough
	 * relative to the node (arms pushed far out, caps nearly meeting) that walk can enclose its area
	 * the other way round. Rather than reason about which arm counts and trim ratios do that, the
	 * shoelace signed area is measured and the order flipped when it is negative -- cheap, and correct
	 * for every configuration including whatever RemoveSelfIntersections' 2-opt reversals leave
	 * behind (a 2-opt move is not orientation-preserving in general).
	 *
	 * Each FBoundaryVertex carries its own Arm/ArcAlpha provenance, and reversing the array moves
	 * that provenance with its vertex, so per-vertex height interpolation downstream is unaffected.
	 */
	void EnsureCounterClockwise(TArray<FBoundaryVertex>& Boundary)
	{
		const int32 NumVerts = Boundary.Num();
		if (NumVerts < 3)
		{
			return;
		}

		double SignedAreaTwice = 0.0;
		for (int32 i = 0; i < NumVerts; ++i)
		{
			const FVector2D& P = Boundary[i].Position;
			const FVector2D& Q = Boundary[(i + 1) % NumVerts].Position;
			SignedAreaTwice += (P.X * Q.Y) - (Q.X * P.Y);
		}

		if (SignedAreaTwice < 0.0)
		{
			Algo::Reverse(Boundary);
		}
	}

	/**
	 * Guarantees a simple (non-self-crossing) polygon regardless of how the boundary was assembled.
	 *
	 * Per-corner geometry only ever reasons about two arms at a time. Given enough arms packed into a
	 * narrow enough wedge, each one's own cap can flare wide enough (roughly atan(HalfWidth/Trim), which
	 * grows as trim is squeezed down to fit the wedge) to overlap a cap two or three arms away rather
	 * than just its immediate neighbour -- a case no purely local, pairwise fix can rule out. Rather
	 * than chase every such configuration individually, this runs the standard 2-opt "uncrossing" pass
	 * used for exactly this class of problem: whenever two non-adjacent edges cross, reversing the
	 * vertex run between them removes that crossing (and only ever shortens the boundary), so repeating
	 * it converges on a simple polygon. This changes only the *order* edges are stitched between
	 * existing vertices -- it neither moves nor drops any vertex, so every arm keeps the exact
	 * connection point its own trim/cap math already decided.
	 */
	void RemoveSelfIntersections(TArray<FBoundaryVertex>& Boundary)
	{
		const int32 NumVerts = Boundary.Num();
		if (NumVerts < 4)
		{
			return;
		}

		int32 SafetyIterations = NumVerts * NumVerts + 16;
		bool bChanged = true;
		while (bChanged && SafetyIterations-- > 0)
		{
			bChanged = false;
			for (int32 i = 0; i < NumVerts && !bChanged; ++i)
			{
				const int32 INext = (i + 1) % NumVerts;
				for (int32 j = i + 2; j < NumVerts && !bChanged; ++j)
				{
					const int32 JNext = (j + 1) % NumVerts;
					if (JNext == i)
					{
						continue; // The two edges already share vertex i -- adjacent, not a crossing.
					}

					if (SegmentsProperlyCross(
						Boundary[i].Position, Boundary[INext].Position,
						Boundary[j].Position, Boundary[JNext].Position))
					{
						// Reverse the run strictly between the two crossing edges -- the classic 2-opt
						// move that swaps a crossing pair of edges for a non-crossing pair joining the
						// same four endpoints.
						int32 Lo = INext;
						int32 Hi = j;
						while (Lo < Hi)
						{
							Boundary.Swap(Lo, Hi);
							++Lo;
							--Hi;
						}
						bChanged = true;
					}
				}
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

namespace
{
	/**
	 * Where two rays actually converge, unlike IntersectPolylines which only looks within the finite
	 * sampled polyline. Two offset sides from arms that meet at a shallow angle can converge well
	 * beyond however far the caller happened to sample the arm -- the true distance is
	 * HalfWidth / tan(halfAngle), which grows without bound as the angle between the arms shrinks, so
	 * no fixed sample length can cover every angle. Both OutTA and OutTB must be >= 0 (the crossing
	 * has to be ahead of both rays); a negative t means the rays are diverging, which is a real open
	 * gap (e.g. a T-junction's straight-through side), not a missed collision.
	 */
	bool RayIntersect(
		const FVector2D& OriginA, const FVector2D& DirA,
		const FVector2D& OriginB, const FVector2D& DirB,
		FVector2D& OutPoint, double& OutTA, double& OutTB)
	{
		const double Denominator = Cross2D(DirA, DirB);
		if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector2D Delta = OriginB - OriginA;
		OutTA = Cross2D(Delta, DirB) / Denominator;
		OutTB = Cross2D(Delta, DirA) / Denominator;

		if (OutTA < 0.0 || OutTB < 0.0)
		{
			return false;
		}

		OutPoint = OriginA + DirA * OutTA;
		return true;
	}
}

FNodeSolution SolveNode(const FVector2D& NodeLocation, TArray<FArm>& Arms, const FNodeOptions& Options)
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
	// Each corner keeps the arc that rounds it, tangent to the two edges that meet there.
	TArray<TArray<FVector2D>> CornerArcs;
	CornerArcs.SetNum(NumArms);
	TArray<bool> HasCollision;
	HasCollision.Init(false, NumArms);

	// Max() (unconstrained) unless a collision is actually found below and a radius resolved for
	// it -- an unconstrained corner (the straight-through gap of a T-junction, say) has no fillet
	// to be tight against, so it should never be what narrows a caller's band width.
	Solution.CornerRadii.Init(TNumericLimits<double>::Max(), NumArms);

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
		const double BaseLenA = PolylineLength(LeftSides[i]);
		const double BaseLenB = PolylineLength(RightSides[Next]);

		if (!IntersectPolylines(LeftSides[i], RightSides[Next], Point, AlongLeft, AlongRight))
		{
			// The bounded search found nothing. Two things produce that: the arms genuinely diverge
			// (an open gap, like a T-junction's straight side), or they converge at an angle so
			// shallow that the true mitre point lies beyond where either road's own surface actually
			// reaches (HalfWidth / tan(halfAngle) grows without bound as the angle shrinks -- several
			// roads clustered a few degrees apart is exactly this). RayIntersect tells the two apart:
			// diverging rays never produce a forward (t >= 0) crossing.
			const FVector2D OriginA = LeftSides[i].Last();
			const FVector2D DirA = DirectionAtDistance(LeftSides[i], BaseLenA);
			const FVector2D OriginB = RightSides[Next].Last();
			const FVector2D DirB = DirectionAtDistance(RightSides[Next], BaseLenB);

			double TA = 0.0;
			double TB = 0.0;
			if (!RayIntersect(OriginA, DirA, OriginB, DirB, Point, TA, TB))
			{
				continue;
			}

			// Converges, but past what either road covers: chasing the spike out to the true mitre
			// point would place the corner far from where the caps (clamped to each arm's own length
			// below) actually end up, tearing the boundary. Bevel between each arm's own end instead
			// -- a flat cut standing in for a mitre neither road is long enough to grow into.
			HasCollision[i] = true;
			ArmConstrained[i] = true;
			ArmConstrained[Next] = true;

			const FVector2D BevelA = PointAtDistance(LeftSides[i], BaseLenA * 0.9);
			const FVector2D BevelB = PointAtDistance(RightSides[Next], BaseLenB * 0.9);
			CornerArcs[i] = { BevelA, BevelB };
			Solution.CornerRadii[i] = 0.0;

			Arms[i].TrimDistance = FMath::Max(
				Arms[i].TrimDistance, ProjectOntoPolyline(Arms[i].Centreline, BevelA));
			Arms[Next].TrimDistance = FMath::Max(
				Arms[Next].TrimDistance, ProjectOntoPolyline(Arms[Next].Centreline, BevelB));
			continue;
		}

		HasCollision[i] = true;
		ArmConstrained[i] = true;
		ArmConstrained[Next] = true;

		// Round the corner with an arc tangent to both edges, the way a real kerb return is built.
		//
		// Both edges leave the collision heading away from the node. A circle of radius R tangent
		// to both touches them at distance R/tan(half-angle) from the corner, centred along the
		// bisector at R/sin(half-angle).
		const FVector2D DirA = DirectionAtDistance(LeftSides[i], AlongLeft);
		const FVector2D DirB = DirectionAtDistance(RightSides[Next], AlongRight);

		const double CosOpening = FMath::Clamp(FVector2D::DotProduct(DirA, DirB), -1.0, 1.0);
		const double HalfAngle = FMath::Acos(CosOpening) * 0.5;

		const double TanHalf = FMath::Tan(HalfAngle);
		const double SinHalf = FMath::Sin(HalfAngle);

		FVector2D TangentA = Point;
		FVector2D TangentB = Point;

		if (TanHalf > 1.0e-4 && SinHalf > 1.0e-4)
		{
			// The tangent points sit further out along each edge than the corner does, so the arc
			// has to fit in the edge that is actually left beyond the collision.
			const double AvailableA = FMath::Max(0.0, BaseLenA - AlongLeft);
			const double AvailableB = FMath::Max(0.0, BaseLenB - AlongRight);
			const double MaxTangent = FMath::Min(AvailableA, AvailableB) * 0.9;

			const double Radius = FMath::Min(Options.CornerRadius, MaxTangent * TanHalf);
			Solution.CornerRadii[i] = Radius;
			const double TangentDistance = Radius / TanHalf;

			if (Radius > WeldTolerance && TangentDistance > WeldTolerance)
			{
				TangentA = Point + DirA * TangentDistance;
				TangentB = Point + DirB * TangentDistance;

				const FVector2D Bisector = (DirA + DirB).GetSafeNormal();
				if (!Bisector.IsNearlyZero())
				{
					const FVector2D Centre = Point + Bisector * (Radius / SinHalf);

					double StartAngle = FMath::Atan2(TangentA.Y - Centre.Y, TangentA.X - Centre.X);
					const double EndAngle = FMath::Atan2(TangentB.Y - Centre.Y, TangentB.X - Centre.X);

					double Sweep = EndAngle - StartAngle;
					while (Sweep > UE_DOUBLE_PI) Sweep -= UE_DOUBLE_PI * 2.0;
					while (Sweep < -UE_DOUBLE_PI) Sweep += UE_DOUBLE_PI * 2.0;

					const int32 Segments = FMath::Clamp(Options.ArcSegments, 1, 32);
					CornerArcs[i].Reserve(Segments + 1);
					for (int32 k = 0; k <= Segments; ++k)
					{
						const double Angle = StartAngle + Sweep * (static_cast<double>(k) / Segments);
						CornerArcs[i].Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
					}
				}
			}
		}

		if (CornerArcs[i].Num() == 0)
		{
			// No room to round it, so the corner stays sharp.
			CornerArcs[i].Add(Point);
		}

		// Trim to the tangent points, not the collision. The arc starts further out along each
		// edge, so trimming only to the corner would leave the curve no room to exist.
		Arms[i].TrimDistance = FMath::Max(
			Arms[i].TrimDistance, ProjectOntoPolyline(Arms[i].Centreline, TangentA));
		Arms[Next].TrimDistance = FMath::Max(
			Arms[Next].TrimDistance, ProjectOntoPolyline(Arms[Next].Centreline, TangentB));
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

	// Boundary, walked in order rather than sorted by angle.
	//
	// A/B Street sorts candidate points around the node and flags that step as fragile — it
	// degrades when arms curve near the node and can emit a self-intersecting bowtie. That sort is
	// only necessary without a known ordering. The arms here are already sorted counter-clockwise,
	// so the boundary can simply be walked: across one arm's cap, round the corner arc, across the
	// next arm's cap. The result is correctly wound by construction and cannot bowtie.
	// Cap ends computed up front, so each corner chain can reference the arm on either side of it.
	TArray<FVector2D> CapRight;
	TArray<FVector2D> CapLeft;
	CapRight.SetNum(NumArms);
	CapLeft.SetNum(NumArms);

	for (int32 i = 0; i < NumArms; ++i)
	{
		const FVector2D End = PointAtDistance(Arms[i].Centreline, Arms[i].TrimDistance);
		const FVector2D Direction = DirectionAtDistance(Arms[i].Centreline, Arms[i].TrimDistance);
		const FVector2D Normal = LeftNormal(Direction);

		CapRight[i] = End - Normal * Arms[i].HalfWidth;
		CapLeft[i] = End + Normal * Arms[i].HalfWidth;
	}

	// Every corner arc above was built from its own collision's *local* view of where the two arms
	// meet. But an arm's final TrimDistance is the max across BOTH of its neighbours, so a corner can
	// go stale: its arc stays anchored to the nearer point it originally found, while the arm it
	// touches got trimmed much further back by its OTHER neighbour (this is exactly what happens when
	// several arms are clustered a few degrees apart -- each pair in the cluster forces the others far
	// back, while a normal-angle neighbour on the far side expected almost no trim at all). A stale
	// arc no longer meets its own arm's cap, and the long, wrongly-placed edge that results is what
	// tears the boundary. Re-anchor any corner whose arc doesn't land exactly on its final caps.
	for (int32 i = 0; i < NumArms; ++i)
	{
		if (!HasCollision[i] || CornerArcs[i].Num() == 0)
		{
			continue;
		}

		const int32 Next = (i + 1) % NumArms;
		const double ToleranceSq = WeldTolerance * WeldTolerance * 4.0;
		const bool bStartMatches = FVector2D::DistSquared(CornerArcs[i][0], CapLeft[i]) < ToleranceSq;
		const bool bEndMatches = FVector2D::DistSquared(CornerArcs[i].Last(), CapRight[Next]) < ToleranceSq;

		if (!bStartMatches || !bEndMatches)
		{
			CornerArcs[i] = { CapLeft[i], CapRight[Next] };
		}
	}

	Solution.RoadsideChains.SetNum(NumArms);

	for (int32 i = 0; i < NumArms; ++i)
	{
		const int32 Next = (i + 1) % NumArms;

		// Right side first, then left: crossing the cap that way is counter-clockwise about
		// the node. Both sit at alpha zero, so they take this arm's height exactly.
		Solution.Boundary.Add({ CapRight[i], i, 0.0f });
		Solution.Boundary.Add({ CapLeft[i], i, 0.0f });

		TArray<FBoundaryVertex>& Chain = Solution.RoadsideChains[i];
		Chain.Add({ CapLeft[i], i, 0.0f });

		if (HasCollision[i])
		{
			// Alpha walks from this arm to the next across the corner, so height crosses with it.
			const int32 ArcCount = CornerArcs[i].Num();
			for (int32 k = 0; k < ArcCount; ++k)
			{
				const float Alpha = (ArcCount > 1)
					? static_cast<float>(k) / static_cast<float>(ArcCount - 1)
					: 0.5f;
				Solution.Boundary.Add({ CornerArcs[i][k], i, Alpha });
				Chain.Add({ CornerArcs[i][k], i, Alpha });
			}
		}

		Chain.Add({ CapRight[Next], i, 1.0f });
	}

	if (Solution.Boundary.Num() < 3)
	{
		return Solution;
	}

	RemoveSelfIntersections(Solution.Boundary);

	// After uncrossing, not before: a 2-opt reversal can itself flip the polygon's orientation, so
	// the winding has to be settled once the vertex order is final.
	EnsureCounterClockwise(Solution.Boundary);

	WeldBoundary(Solution.Boundary);
	if (Solution.Boundary.Num() >= 2 &&
		FVector2D::DistSquared(Solution.Boundary[0].Position, Solution.Boundary.Last().Position)
			< WeldTolerance * WeldTolerance)
	{
		Solution.Boundary.Pop();
	}

	Solution.bValid = Solution.Boundary.Num() >= 3;
	return Solution;
}

bool IsRampConnection(double AngleGapRadians, int32 ArmDrivingLanes, int32 OtherDrivingLanes)
{
	return FMath::Abs(AngleGapRadians) < RampAngleThresholdRadians && ArmDrivingLanes != OtherDrivingLanes;
}
}
