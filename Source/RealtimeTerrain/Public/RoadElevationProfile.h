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
}
