// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ARealtimeRoadSplineActor;
class ARealtimeRoadJunctionActor;

/**
 * Turning a point on a road into an intersection.
 *
 * This is the piece the point-selection workflow is built on: pick somewhere along a road, say
 * which directions other roads leave in, and get back a junction with the road already cut around
 * it and both exposed ends pinned to their leg sockets.
 */
namespace RoadNetwork
{
	/**
	 * Creates a junction where a road passes through the given distance along its spline, cuts the
	 * road around the junction's footprint, and wires both exposed ends to their legs.
	 *
	 * The road contributes two legs — the stretch behind and the stretch ahead — unless the
	 * distance sits at one of its ends, in which case it contributes one and the junction becomes
	 * that end's terminus instead of a cut. ExtraHeadingsDegrees adds further legs for roads
	 * branching away; each one's assigned index comes back through OutExtraLegIndices so the
	 * caller can attach a road to it.
	 *
	 * Returns null if the road is too short, or if fewer than two legs would result.
	 */
	ARealtimeRoadJunctionActor* CreateJunctionOnRoad(
		ARealtimeRoadSplineActor* Road,
		double DistanceAlong,
		const TArray<float>& ExtraHeadingsDegrees,
		TArray<int32>& OutExtraLegIndices);

	/**
	 * Arm length that leaves every corner's fillet somewhere to live.
	 *
	 * The corner between two adjacent legs sits at roughly halfWidth / tan(gap/2) from the centre,
	 * so the tightest gap decides how far the arms have to reach. A fixed arm length is what makes
	 * narrow-angle intersections — the six-way star being the obvious case — collapse into
	 * overlapping geometry.
	 */
	double ComputeArmLength(const TArray<float>& HeadingsDegrees, double MaxHalfWidth);

	/** Distance along a road's spline nearest to a world location. */
	bool FindDistanceAlongRoad(const ARealtimeRoadSplineActor* Road, const FVector& WorldLocation, double& OutDistance);

	/**
	 * Distance along a road's spline nearest to a world location, measured in plan only.
	 *
	 * The spline's own closest-point query is three-dimensional, so it is thrown off by any height
	 * difference between the query point and the road. A junction sitting at a different height
	 * from the roads it joins was picking node distances from entirely the wrong part of each
	 * spline, which scrambled the arm headings that follow from them.
	 */
	bool FindDistanceAlongRoad2D(const ARealtimeRoadSplineActor* Road, const FVector2D& WorldXY, double& OutDistance);

	/** A place where road centrelines cross, and the roads involved. */
	struct FCrossing
	{
		FVector2D Location = FVector2D::ZeroVector;
		TArray<ARealtimeRoadSplineActor*> Roads;

		/** Widest road meeting here, used to size the node. */
		double MaxRoadWidth = 0.0;
	};

	/**
	 * Finds everywhere road centrelines cross in plan.
	 *
	 * Crossings closer together than MergeRadius collapse into one node, so a pair of roads that
	 * touch twice in quick succession, or three roads meeting at almost but not exactly a point,
	 * produce a single intersection rather than a cluster of overlapping ones.
	 *
	 * OnlyInvolvingRoad, when set, skips every pair that doesn't include it -- cutting the search
	 * from every-road-against-every-road down to one-against-every-other. Only the road actually
	 * moving can have gained or lost a crossing since the last full pass, so this is exact, not an
	 * approximation: it just skips pairs that provably could not have changed.
	 */
	void DetectCrossings(
		UWorld* World, double MergeRadius, TArray<FCrossing>& OutCrossings,
		const ARealtimeRoadSplineActor* OnlyInvolvingRoad = nullptr);

	/**
	 * Brings the world's junctions in line with where roads actually cross: one at each detected
	 * crossing, existing ones moved and re-solved, and any previously generated junction whose
	 * crossing has gone destroyed.
	 *
	 * Only junctions the detector generated are touched; anything placed by hand is left alone.
	 * Returns how many generated junctions are live afterwards.
	 */
	int32 RebuildIntersections(UWorld* World);

	/**
	 * The same reconciliation as RebuildIntersections, scoped to crossings involving one road.
	 *
	 * Built for the interactive drag path: re-solving every auto-generated junction in the level on
	 * every throttled tick (RebuildIntersections's full O(roads^2) crossing scan, repeated ~12x a
	 * second) is a real, felt stutter in anything but a small level, and almost all of that work is
	 * wasted -- a drag can only possibly create, move, or remove a crossing that the road being
	 * dragged is part of. This creates/updates junctions for FocusRoad's own crossings exactly like
	 * RebuildIntersections does (so the junction the player is dragging toward still reshapes live,
	 * with no visible change in behaviour), but deliberately skips the orphan-cleanup pass -- a
	 * crossing between two OTHER roads that this scoped scan didn't look at is not evidence it
	 * disappeared, only that it wasn't checked this time. Cleanup still happens, just at the next full
	 * RebuildIntersections (every drag ends by calling one), so nothing is left permanently stale.
	 */
	int32 RebuildIntersectionsNear(UWorld* World, ARealtimeRoadSplineActor* FocusRoad);
}
