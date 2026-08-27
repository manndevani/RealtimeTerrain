// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadWidthProfile.h"

namespace RoadWidthProfile
{
	double EvaluateWidth(const TArray<FRoadWidthKey>& Keys, double Dist)
	{
		if (Keys.Num() == 0)
		{
			return 1.0;
		}

		if (Keys.Num() == 1)
		{
			const double DeltaDist = FMath::Abs(Dist - Keys[0].Dist);
			if (DeltaDist >= SingleKeyFalloffRadius)
			{
				return 1.0;
			}
			// Cubic smoothstep (3a^2 - 2a^3) rather than a linear blend -- a linear falloff has a
			// visible tangent kink right at the key, which reads as a crease in the road edge instead
			// of a smooth bulge.
			const double Alpha = 1.0 - DeltaDist / SingleKeyFalloffRadius;
			const double Smooth = Alpha * Alpha * (3.0 - 2.0 * Alpha);
			return FMath::Lerp(1.0, Keys[0].WidthMultiplier, Smooth);
		}

		if (Dist < Keys[0].Dist || Dist > Keys.Last().Dist)
		{
			return 1.0;
		}

		int32 Index = RoadCurveGraph::FindElementIndex(Keys, Dist);
		// Clamp so Index+1 is always a valid bracketing key, matching RoadElevationProfile::EvaluateHeight.
		if (Index == Keys.Num() - 1)
		{
			Index--;
		}

		const FRoadWidthKey& Start = Keys[Index];
		const FRoadWidthKey& End = Keys[Index + 1];
		const double SegmentLength = End.Dist - Start.Dist;
		// Same zero-length-segment guard as RoadElevationProfile::EvaluateHeight -- two keys authored
		// at the same Dist would otherwise divide by zero and contaminate the road width with NaN.
		if (SegmentLength <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Start.WidthMultiplier;
		}
		const double S = Dist - Start.Dist;
		return FMath::CubicInterp(Start.WidthMultiplier, Start.Slope * SegmentLength, End.WidthMultiplier, End.Slope * SegmentLength, S / SegmentLength);
	}
}
