// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadElevationProfile.h"

namespace RoadElevationProfile
{
	double EvaluateHeight(const TArray<FRoadHeightKey>& Keys, double Dist)
	{
		int32 Index = RoadCurveGraph::FindElementIndex(Keys, Dist);
		// Clamp so Index+1 is always a valid bracketing key, matching RoadCurveGraph::FindBracketIndex.
		if (Index == Keys.Num() - 1)
		{
			Index--;
		}

		const FRoadHeightKey& Start = Keys[Index];
		const FRoadHeightKey& End = Keys[Index + 1];
		const double SegmentLength = End.Dist - Start.Dist;
		const double S = Dist - Start.Dist;
		return FMath::CubicInterp(Start.Height, Start.Slope * SegmentLength, End.Height, End.Slope * SegmentLength, S / SegmentLength);
	}
}
