// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadStylePresets.h"

int32 URoadStylePreset::NumDrivingLanes() const
{
	// Ported from RoadBuilder's URoadStyle::NumDrivingLanes.
	int32 Count = 0;
	for (const FRoadStyleLaneEntry& Lane : LeftLanes)
	{
		if (Lane.LaneType == ERealtimeLaneType::Driving)
		{
			Count++;
		}
	}
	for (const FRoadStyleLaneEntry& Lane : RightLanes)
	{
		if (Lane.LaneType == ERealtimeLaneType::Driving)
		{
			Count++;
		}
	}
	return Count;
}

double URoadStylePreset::TotalWidth() const
{
	double Total = 0.0;
	for (const FRoadStyleLaneEntry& Lane : LeftLanes)
	{
		Total += Lane.Width;
	}
	for (const FRoadStyleLaneEntry& Lane : RightLanes)
	{
		Total += Lane.Width;
	}
	return Total;
}

double URoadStylePreset::DrivingSurfaceWidth() const
{
	auto SumDrivingSurface = [](const TArray<FRoadStyleLaneEntry>& Lanes) -> double
	{
		double Total = 0.0;
		for (const FRoadStyleLaneEntry& Lane : Lanes)
		{
			if (Lane.LaneType == ERealtimeLaneType::Driving || Lane.LaneType == ERealtimeLaneType::Shoulder)
			{
				Total += Lane.Width;
			}
		}
		return Total;
	};
	return SumDrivingSurface(LeftLanes) + SumDrivingSurface(RightLanes);
}
