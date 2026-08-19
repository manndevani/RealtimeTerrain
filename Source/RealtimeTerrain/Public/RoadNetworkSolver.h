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

		/** Opaque caller handle, carried through so the result can be mapped back to a road. */
		int32 UserData = INDEX_NONE;

		//--- filled in by SolveNode ---

		/** Distance along Centreline the road must be cut back to, to clear the intersection. */
		double TrimDistance = 0.0;

		/** Heading of the arm leaving the node, in radians. Used for the angular sort. */
		double AngleRad = 0.0;
	};

	struct FNodeSolution
	{
		/** Counter-clockwise boundary of the intersection surface in world XY. */
		TArray<FVector2D> Boundary;

		/** False when the node had too few arms, or the boundary collapsed. */
		bool bValid = false;
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
	FNodeSolution SolveNode(const FVector2D& NodeLocation, TArray<FArm>& Arms);
}
