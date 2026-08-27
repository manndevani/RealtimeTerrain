// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoadCurveGraph.h"
#include "RoadWidthProfile.generated.h"

/**
 * One key of an authored road width profile -- same shape as RoadElevationProfile.h's
 * FRoadHeightKey (Dist/Value/Slope, cubic-Hermite), kept as its own struct for the same reason
 * that one is kept distinct from RoadCurveGraph.h's FRoadCurveOffsetKey: "WidthMultiplier" reads
 * wrong as a height or an offset, and each channel gets its own semantically-named key type.
 *
 * WidthMultiplier scales the road's authored cross-section width at Dist -- 1.0 is the road's own
 * unmodified width, 0.5 half width, 2.0 double width. Gizmo Scale mode on a selected road control
 * point (URealtimeRoadBuilderComponent) writes one of these at that point's distance-along-spline.
 */
USTRUCT(BlueprintType)
struct FRoadWidthKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Width)
	double Dist = 0.0;

	UPROPERTY(EditAnywhere, Category = Width)
	double WidthMultiplier = 1.0;

	/** Tangent (dWidthMultiplier/dDist) at this key, matching FRoadHeightKey::Slope's convention. */
	UPROPERTY(EditAnywhere, Category = Width)
	double Slope = 0.0;

	bool operator<(const FRoadWidthKey& Other) const { return Dist < Other.Dist; }
};

namespace RoadWidthProfile
{
	/**
	 * A single authored key with no bracketing partner yet blends to the unscaled 1.0 baseline over
	 * this radius (world cm) on either side -- see EvaluateWidth's 1-key case. HeightKeys has no
	 * equivalent because an elevation profile is only ever meaningful in bracketed pairs (a bridge's
	 * start and end); a single width edit ("make the road wider right here") is the natural first
	 * gesture the Scale-mode gizmo produces, so it needs to visibly do something on its own.
	 */
	constexpr double SingleKeyFalloffRadius = 500.0;

	/**
	 * Effective width multiplier at Dist, given Keys (sorted ascending by Dist -- callers own the
	 * sort, same contract as RoadElevationProfile::EvaluateHeight). Unlike that function, this one
	 * is a complete "what's the width here" answer on its own for every key count, not just >= 2:
	 *  - 0 keys: 1.0 everywhere (unscaled).
	 *  - 1 key: 1.0 outside SingleKeyFalloffRadius, smoothly blending to the key's own
	 *    WidthMultiplier within it.
	 *  - 2+ keys: cubic-Hermite between the bracketing pair (same FindElementIndex binary search
	 *    EvaluateHeight uses), clamped to 1.0 outside the authored range on either end.
	 */
	REALTIMETERRAIN_API double EvaluateWidth(const TArray<FRoadWidthKey>& Keys, double Dist);
}
