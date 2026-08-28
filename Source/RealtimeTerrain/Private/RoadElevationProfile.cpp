// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadElevationProfile.h"

namespace RoadElevationProfile
{
	double EvaluateHeight(const TArray<FRoadHeightKey>& Keys, double Dist)
	{
		if (Keys.Num() == 0)
		{
			return 0.0;
		}
		if (Keys.Num() == 1)
		{
			return Keys[0].Height;
		}

		int32 Index = RoadCurveGraph::FindElementIndex(Keys, Dist);
		// Clamp so Index+1 is always a valid bracketing key, matching RoadCurveGraph::FindBracketIndex.
		if (Index >= Keys.Num() - 1)
		{
			Index = Keys.Num() - 2;
		}
		if (Index < 0)
		{
			Index = 0;
		}

		const FRoadHeightKey& Start = Keys[Index];
		const FRoadHeightKey& End = Keys[Index + 1];
		const double SegmentLength = End.Dist - Start.Dist;
		// Two keys authored at (or rounded to) the same Dist give SegmentLength == 0 -- S/SegmentLength
		// would then be 0/0 (Dist == Start.Dist here too) or worse for Dist fractionally off Start.Dist,
		// and a single NaN height silently discards every triangle touching that vertex with no error
		// (the exact failure mode RebuildMesh's own height sanitization exists to catch elsewhere).
		if (SegmentLength <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Start.Height;
		}
		const double S = Dist - Start.Dist;
		return FMath::CubicInterp(Start.Height, Start.Slope * SegmentLength, End.Height, End.Slope * SegmentLength, S / SegmentLength);
	}

	void DetectFlyoverBridges(
		const TArray<double>& Distances,
		const TArray<double>& Heights,
		double MaxGradeRadians,
		double MinRunLength,
		TArray<FRoadHeightKey>& OutBridgeKeys)
	{
		OutBridgeKeys.Reset();
		const int32 Num = Distances.Num();
		if (Num < 2 || Heights.Num() != Num)
		{
			return;
		}

		auto GradeAt = [&Distances, &Heights](int32 i) -> double
		{
			return FMath::Atan2(Heights[i + 1] - Heights[i], Distances[i + 1] - Distances[i]);
		};

		int32 i = 0;
		while (i < Num - 1)
		{
			if (FMath::Abs(GradeAt(i)) <= MaxGradeRadians)
			{
				i++;
				continue;
			}

			// A rough run starts here -- extend it forward while the grade stays over threshold.
			const int32 RunStart = i;
			int32 RunEnd = i + 1;
			while (RunEnd < Num - 1 && FMath::Abs(GradeAt(RunEnd)) > MaxGradeRadians)
			{
				RunEnd++;
			}

			if (Distances[RunEnd] - Distances[RunStart] >= MinRunLength)
			{
				FRoadHeightKey StartKey;
				StartKey.Dist = Distances[RunStart];
				StartKey.Height = Heights[RunStart];
				StartKey.Slope = 0.0;

				FRoadHeightKey EndKey;
				EndKey.Dist = Distances[RunEnd];
				EndKey.Height = Heights[RunEnd];
				EndKey.Slope = 0.0;

				OutBridgeKeys.Add(StartKey);
				OutBridgeKeys.Add(EndKey);
			}

			i = RunEnd + 1;
		}
	}
}
