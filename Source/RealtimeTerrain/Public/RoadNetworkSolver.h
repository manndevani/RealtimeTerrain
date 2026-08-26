// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Intersection geometry solved from the roads that meet at a point.
 *
 * The approach follows the one A/B Street documents for turning OSM centrelines into street
 * surfaces (https://a-b-street.github.io/docs/tech/map/geometry/index.html): offset every incident
 * road to its two sides, collide the sides of angularly adjacent roads, use those collisions to
 * decide how far each road must be trimmed back, and build the intersection surface from the
 * trimmed ends plus the collision points themselves.
 *
 * That is a meaningful step up from picking a single arm length per node. Trim distance falls out
 * of the geometry rather than a heuristic, roads of different widths are handled without a special
 * case, and because the collisions are polyline-to-polyline rather than line-to-line it works on
 * curved approaches instead of assuming every arm leaves the node straight.
 *
 * A/B Street is candid about where the method struggles, and the same traps apply here: sorting
 * boundary points by angle degrades when roads curve sharply near the node, mitre joins run away at
 * very acute angles unless capped, and near-duplicate points have to be welded or the polygon
 * self-intersects. Each is handled explicitly below.
 */
namespace RoadNetworkSolver
{
	/** One road arriving at a node. */
	struct FArm
	{
		/**
		 * Centreline in world XY, ordered so that index 0 sits at the node and the polyline runs
		 * away from it. Callers with a road that meets the node at its far end pass it reversed.
		 */
		TArray<FVector2D> Centreline;

		/** Half the full surface width of this road, including anything the surface must cover. */
		double HalfWidth = 400.0;

		/**
		 * Driving lanes this road carries into the node. Used only for ramp/fork classification
		 * (see IsRampConnection below) -- SolveNode's own boundary/trim math is width-based and
		 * does not read this.
		 */
		int32 DrivingLaneCount = 1;

		/** Opaque caller handle, carried through so the result can be mapped back to a road. */
		int32 UserData = INDEX_NONE;

		//--- filled in by SolveNode ---

		/** Distance along Centreline the road must be cut back to, to clear the intersection. */
		double TrimDistance = 0.0;

		/** Heading of the arm leaving the node, in radians. Used for the angular sort. */
		double AngleRad = 0.0;
	};

	/** Tuning for how the intersection surface is shaped. */
	struct FNodeOptions
	{
		/**
		 * Radius of the circular arc rounding each outer corner, in cm.
		 *
		 * The corner between two adjacent arms is an arc tangent to both of their facing edges,
		 * which is how real kerb returns are built. Clamped per corner against the edge length
		 * actually available, so a tight corner rounds by less rather than overshooting.
		 *
		 * This drives how large the whole intersection becomes, and is the main dial for its look.
		 * The tangent point sits Radius/tan(half-angle) beyond where the two edges cross, and every
		 * arm has to be trimmed back that far to leave the curve room — so on a square four-way,
		 * each arm trims by half its width plus roughly this radius. Around a third of the road
		 * width reads as a normal street corner; larger values give sweeping highway-style returns
		 * and a much bigger junction.
		 */
		double CornerRadius = 300.0;

		/** Segments per corner arc. */
		int32 ArcSegments = 8;

		/**
		 * Multiplies CornerRadius at a corner IsRampConnection classifies as a ramp/fork, so a
		 * highway on/off-ramp reads as a long, gentle merge taper rather than the same tight kerb-
		 * return radius an ordinary street corner gets. Still clamped against the edge length
		 * actually available, same as any other corner, so it narrows rather than overshoots when
		 * there isn't room for the full multiplied radius.
		 */
		double RampCornerRadiusMultiplier = 3.0;
	};

	/**
	 * One vertex of the intersection outline, and where it came from.
	 *
	 * The provenance is what lets the surface stop being flat. A vertex on an arm's cap belongs to
	 * that road and should sit at its height; a vertex partway round a corner belongs between two
	 * roads and should blend between them. Without this the caller only has positions in plan and
	 * no way to decide a height other than picking one for the whole junction.
	 */
	struct FBoundaryVertex
	{
		FVector2D Position = FVector2D::ZeroVector;

		/** Arm this vertex leaves from, indexing into the sorted Arms array. */
		int32 Arm = 0;

		/** 0 on that arm's own cap, rising to 1 at the next arm's cap around the corner. */
		float ArcAlpha = 0.0f;
	};

	struct FNodeSolution
	{
		/** Counter-clockwise boundary of the intersection surface in world XY. */
		TArray<FBoundaryVertex> Boundary;

		/**
		 * Per corner, the stretch of boundary running between two adjacent arms.
		 *
		 * This is the part of the perimeter that is roadside rather than a connection to a road,
		 * so it is where kerbs and footpaths belong. The arm caps are deliberately excluded: those
		 * abut the road that continues, and putting a kerb across them would wall the road off.
		 */
		TArray<TArray<FBoundaryVertex>> RoadsideChains;

		/** False when the node had too few arms, or the boundary collapsed. */
		bool bValid = false;

		/**
		 * Per corner, parallel to RoadsideChains: the actual fillet radius SolveNode resolved there
		 * (already clamped against the edge length available, per FNodeOptions::CornerRadius's own
		 * doc comment), or TNumericLimits<double>::Max() where the corner is unconstrained -- no
		 * adjacent-arm collision at all, e.g. the straight-through gap of a T-junction.
		 *
		 * Exists so a caller building a roadside kerb/footpath band can size it against the corner
		 * that actually has the least room, the same way BuildBoundary's fallback path already does
		 * for its own corners, rather than assuming the solver's output can carry any band width.
		 */
		TArray<double> CornerRadii;
	};

	/**
	 * Offsets a polyline sideways. Positive Distance is to the left of travel.
	 *
	 * Joins are mitred, but a mitre at a sharp corner shoots off toward infinity, so it is capped at
	 * MaxMiterScale times the offset distance and falls back to a bevel beyond that.
	 */
	void OffsetPolyline(
		const TArray<FVector2D>& In,
		double Distance,
		TArray<FVector2D>& Out,
		double MaxMiterScale = 4.0);

	/**
	 * Furthest crossing between two polylines, measured along A.
	 *
	 * The furthest one is what matters: it is the collision that forces the largest trim, and
	 * taking anything nearer leaves the two road surfaces still overlapping.
	 */
	bool IntersectPolylines(
		const TArray<FVector2D>& A,
		const TArray<FVector2D>& B,
		FVector2D& OutPoint,
		double& OutDistanceAlongA,
		double& OutDistanceAlongB);

	/** Distance along a polyline of the point on it closest to Query. */
	double ProjectOntoPolyline(const TArray<FVector2D>& Polyline, const FVector2D& Query);

	/** Point at a distance along a polyline, clamped to its ends. */
	FVector2D PointAtDistance(const TArray<FVector2D>& Polyline, double Distance);

	/** Direction of travel at a distance along a polyline. */
	FVector2D DirectionAtDistance(const TArray<FVector2D>& Polyline, double Distance);

	/** Total length of a polyline. */
	double PolylineLength(const TArray<FVector2D>& Polyline);

	/**
	 * Solves an intersection.
	 *
	 * Arms are sorted counter-clockwise in place and have TrimDistance and AngleRad written into
	 * them, so the caller can cut each road back by the amount this decided.
	 */
	FNodeSolution SolveNode(
		const FVector2D& NodeLocation,
		TArray<FArm>& Arms,
		const FNodeOptions& Options = FNodeOptions());

	/** Below this angular gap between two arms, they read as a shallow merge/diverge rather than a crossing. */
	constexpr double RampAngleThresholdRadians = 0.6981317008; // 40 degrees

	/**
	 * True when a pair of adjacent arms should be classified as a ramp/fork connection -- a
	 * shallow-angle join between roads whose driving-lane counts differ -- rather than a plain
	 * crossing where roads of matching lane count meet closer to perpendicular.
	 *
	 * Ported from the idea behind RoadBuilder's FJunctionGate::IsRampOf (RoadScene.h), adapted:
	 * that method additionally walks a ConnectedParents lineage graph RoadBuilder's own road
	 * actors maintain, which this system has no equivalent of -- here, a junction already knows
	 * which roads meet it via FRoadJunctionCut, so classification uses angle and lane-count delta
	 * directly rather than parentage.
	 */
	bool IsRampConnection(double AngleGapRadians, int32 ArmDrivingLanes, int32 OtherDrivingLanes);
}
