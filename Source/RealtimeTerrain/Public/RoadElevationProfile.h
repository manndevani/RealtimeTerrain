// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoadCurveGraph.h"
#include "RoadElevationProfile.generated.h"

/**
 * One key of an authored road elevation profile, ported from RoadBuilder's FHeightPoint/
 * FHeightSegment (RoadActor.h). Height here is data, evaluated independently of terrain
 * sampling -- that decoupling is the entire mechanism behind an "Elevated" road: a road with
 * height keys simply stops asking the terrain what its Z should be at those stations and follows
 * this cubic-Hermite profile instead.
 *
 * Same shape as RoadCurveGraph.h's FRoadCurveOffsetKey (Dist/Value/Slope, cubic-Hermite) --
 * kept as a distinct, semantically-named struct rather than reusing that one directly, since
 * "Offset" reads wrong for a height channel and this session's own established pattern already
 * ported RoadBuilder's boundary/lane offset keys under their own name for the same reason.
 */
USTRUCT()
struct FRoadHeightKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Height)
	double Dist = 0.0;

	UPROPERTY(EditAnywhere, Category = Height)
	double Height = 0.0;

	/** Tangent (dHeight/dDist) at this key, matching FRoadCurveOffsetKey::Slope's convention. */
	UPROPERTY(EditAnywhere, Category = Height)
	double Slope = 0.0;

	bool operator<(const FRoadHeightKey& Other) const { return Dist < Other.Dist; }
};

namespace RoadElevationProfile
{
	/**
	 * Cubic-Hermite height at Dist, bracketed and interpolated the same way
	 * RoadCurveGraph::EvaluateOffset is (reusing its FindElementIndex binary search -- the
	 * complex part of that logic -- rather than re-deriving it). Ported from RoadBuilder's
	 * FHeightSegment::Get. Requires at least 2 keys, sorted ascending by Dist.
	 */
	REALTIMETERRAIN_API double EvaluateHeight(const TArray<FRoadHeightKey>& Keys, double Dist);

	/**
	 * Scans a terrain-sampled height sequence for stretches whose local grade exceeds
	 * MaxGradeRadians for at least MinRunLength, and returns a bridging pair of zero-slope
	 * FRoadHeightKeys (a comfortable departure and return grade, matching the S-curve
	 * EvaluateHeight already gives a zero-slope-ended profile) for each stretch found -- the
	 * automatic flyover trigger: instead of a road following every bump, the stations between a
	 * detected pair's two keys should have their height overridden by EvaluateHeight against a
	 * profile built from these keys instead of the raw terrain sample.
	 *
	 * RoadBuilder's own FHeightPoint/FHeightSegment has no equivalent of this -- its height keys
	 * are always hand-authored. This is new behaviour built on top of the ported math, not a port
	 * of an existing RoadBuilder feature.
	 *
	 * Bridge pairs are returned in a flat list, two keys per stretch found, in ascending Dist
	 * order overall -- ready to merge with a caller's own manually-authored keys (which should
	 * always take priority over an auto-detected bridge that would otherwise cover the same
	 * stretch) into one effective profile.
	 *
	 * Distances/Heights must be the same length, at least 2 entries, and sorted ascending by
	 * distance -- matching a sampled road centreline.
	 */
	REALTIMETERRAIN_API void DetectFlyoverBridges(
		const TArray<double>& Distances,
		const TArray<double>& Heights,
		double MaxGradeRadians,
		double MinRunLength,
		TArray<FRoadHeightKey>& OutBridgeKeys);
}
