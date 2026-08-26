// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadPierPlacement.h"

void RoadPierPlacement::PlacePiers(
	const TArray<double>& Distances,
	const TArray<FVector2D>& StationXY,
	const TArray<double>& RoadUndersideZ,
	const TArray<double>& GroundZ,
	double MinGap,
	double SpacingAlongRoad,
	TArray<FPierPlacement>& OutPlacements)
{
	OutPlacements.Reset();

	const int32 Num = Distances.Num();
	if (Num < 2 || SpacingAlongRoad <= 0.0) return;
	if (StationXY.Num() != Num || RoadUndersideZ.Num() != Num || GroundZ.Num() != Num) return;

	const double TotalLength = Distances.Last();
	int32 Cursor = 0;

	for (double Target = 0.0; Target <= TotalLength; Target += SpacingAlongRoad)
	{
		// Distances advance monotonically with Target, so this walks forward from the last cursor
		// rather than searching from the start each time -- same pattern as GenerateScatter's
		// FrameCursor walk.
		while (Cursor + 1 < Num && Distances[Cursor + 1] < Target)
		{
			++Cursor;
		}

		const double Gap = RoadUndersideZ[Cursor] - GroundZ[Cursor];
		if (Gap > MinGap)
		{
			FPierPlacement Placement;
			Placement.LocationXY = StationXY[Cursor];
			Placement.GroundZ = GroundZ[Cursor];
			Placement.Height = Gap;
			OutPlacements.Add(Placement);
		}
	}
}
