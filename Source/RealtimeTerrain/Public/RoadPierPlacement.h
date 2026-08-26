// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Pure placement math for elevated-road pier supports. RoadBuilder has no pier geometry at all --
 * its "Elevated" road style is visual only -- so this is new behaviour built to close that gap:
 * any stretch where ApplyElevationProfile (RoadElevationProfile.h) has lifted the road meaningfully
 * above the raw terrain sample gets support piers spanning from the ground up to the road
 * underside, spaced evenly along the elevated run.
 *
 * Kept independent of Unreal's mesh/instancing APIs (HISM, static meshes, actors) so the placement
 * decision -- where a pier goes and how tall it is -- is testable without a spawned actor or
 * loaded assets, matching this project's established pattern for RoadElevationProfile::
 * DetectFlyoverBridges.
 */
namespace RoadPierPlacement
{
	struct FPierPlacement
	{
		FVector2D LocationXY = FVector2D::ZeroVector;
		double GroundZ = 0.0;
		/** Span from GroundZ up to the road underside at this station. */
		double Height = 0.0;
	};

	/**
	 * Walks a sampled road centreline looking for stretches where the road sits more than MinGap
	 * above the ground, and returns placements roughly SpacingAlongRoad apart within each such
	 * stretch (evaluated at the nearest sampled station at or before each target distance, so
	 * placements land on real stations rather than interpolated ones).
	 *
	 * Distances/StationXY/RoadUndersideZ/GroundZ are parallel per-station arrays -- same length,
	 * Distances sorted ascending -- matching a sampled road centreline. RoadUndersideZ is the final
	 * (post elevation-profile) road height minus deck thickness; GroundZ is the raw terrain sample
	 * at that station, independent of the road's own height.
	 */
	REALTIMETERRAIN_API void PlacePiers(
		const TArray<double>& Distances,
		const TArray<FVector2D>& StationXY,
		const TArray<double>& RoadUndersideZ,
		const TArray<double>& GroundZ,
		double MinGap,
		double SpacingAlongRoad,
		TArray<FPierPlacement>& OutPlacements);
}
