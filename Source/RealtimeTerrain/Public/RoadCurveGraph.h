// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoadCurveGraph.generated.h"

/**
 * A cubic-Hermite lateral-offset key, ported from RoadBuilder's FCurveOffset
 * (Plugins/RoadBuilder-master/Source/RoadBuilder/Public/RoadCurve.h).
 *
 * A key list describes a boundary or lane's lateral offset from the road's own centreline as a
 * function of distance along it -- e.g. a shoulder widening, a median tapering to a point, a lane
 * dropping out at a ramp -- independent of every other boundary's own key list. Slope is the
 * offset's rate of change at this key (Offset units per Dist unit), matching FCurveOffset::Dir.
 */
USTRUCT()
struct FRoadCurveOffsetKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Offset)
	double Dist = 0.0;

	UPROPERTY(EditAnywhere, Category = Offset)
	double Offset = 0.0;

	UPROPERTY(EditAnywhere, Category = Offset)
	double Slope = 0.0;

	bool operator<(const FRoadCurveOffsetKey& Other) const { return Dist < Other.Dist; }
};

namespace RoadCurveGraph
{
	/**
	 * Index i such that Keys[i].Dist <= Dist, clamped so i+1 is always a valid bracketing key.
	 * Ported from RoadBuilder's GetElementIndex/GetPointIndex (RoadCurve.h).
	 */
	REALTIMETERRAIN_API int32 FindBracketIndex(const TArray<FRoadCurveOffsetKey>& Keys, double Dist);

	/** Cubic-Hermite offset at Dist. Ported from FCurveOffset::Get. Requires at least 2 keys. */
	REALTIMETERRAIN_API double EvaluateOffset(const TArray<FRoadCurveOffsetKey>& Keys, double Dist);

	/**
	 * Additional heading (radians) the offset curve's own tangent contributes at Dist, on top of
	 * the base centreline's heading. Ported from FCurveOffset::GetR.
	 */
	REALTIMETERRAIN_API double EvaluateHeadingDelta(const TArray<FRoadCurveOffsetKey>& Keys, double Dist);

	/**
	 * Re-interpolates a key list to the sub-range [Start, End], re-basing distances to start at 0.
	 * Ported from URoadCurve::CutOffsets: every key inside the range survives; the range's own
	 * endpoints are added via cubic interpolation so the trimmed curve's shape is unchanged, not
	 * just clipped.
	 */
	REALTIMETERRAIN_API TArray<FRoadCurveOffsetKey> CutOffsets(const TArray<FRoadCurveOffsetKey>& Keys, double Start, double End);

	/**
	 * Drops keys that are redundant -- nearly coincident in distance with a neighbour, or adding no
	 * shape beyond what a straight line between its neighbours already gives. Ported from
	 * URoadCurve::CleanOffsets.
	 */
	REALTIMETERRAIN_API void CleanOffsets(TArray<FRoadCurveOffsetKey>& Keys);
}

/**
 * Base class for a boundary or lane's authored lateral-offset curve, ported from RoadBuilder's
 * URoadCurve (RoadCurve.h). Offsets are relative to the owning road's own centreline rather than
 * an independent world-space polyline: every boundary/lane on a road rides the same terrain-
 * conformed centreline TerraDyne already computes, so duplicating position/tangent data per
 * boundary would be pure redundancy. Only the lateral shape is authored per curve.
 */
UCLASS()
class REALTIMETERRAIN_API URealtimeRoadCurve : public UObject
{
	GENERATED_BODY()

public:
	/** Authored, hand-editable keys. Ported from URoadCurve::LocalOffsets. */
	UPROPERTY(EditAnywhere, Category = "Road Curve")
	TArray<FRoadCurveOffsetKey> LocalOffsets;

	/**
	 * Effective keys after inheriting from a neighbouring curve (ported from URoadCurve::Offsets).
	 * For a curve with no neighbour to inherit from, this equals LocalOffsets -- call
	 * RecomputeEffectiveOffsets() after any edit to LocalOffsets to keep it in sync.
	 */
	UPROPERTY()
	TArray<FRoadCurveOffsetKey> Offsets;

	double GetOffset(double Dist) const { return RoadCurveGraph::EvaluateOffset(Offsets, Dist); }
	double GetHeadingDelta(double Dist) const { return RoadCurveGraph::EvaluateHeadingDelta(Offsets, Dist); }
	double Length() const { return Offsets.Num() ? Offsets.Last().Dist : 0.0; }

	void RecomputeEffectiveOffsets() { Offsets = LocalOffsets; }

	/**
	 * Inserts a key at Dist carrying the raw offset value of the key bracketing it, so a
	 * subsequent Cut() can discard everything outside [Start, End] without altering LocalOffsets'
	 * shape inside that range. Ported from URoadCurve::AddLocalOffset -- note this carries the
	 * bracket's raw Offset value forward rather than the curve's true interpolated value at Dist,
	 * matching RoadBuilder's own behaviour exactly rather than a "corrected" version of it.
	 */
	int32 AddLocalOffset(double Dist);

	/** Index of a LocalOffsets key at Dist within Tolerance, or INDEX_NONE. Ported from URoadCurve::GetLocalOffset. */
	int32 FindLocalOffset(double Dist, double Tolerance) const;

	/**
	 * Trims LocalOffsets to [Start, End] and re-bases distances to start at 0. Ported from
	 * URoadCurve::Cut. Does not update Offsets -- call RecomputeEffectiveOffsets() afterward.
	 */
	void Cut(double Start, double End);

	/**
	 * Appends Other's LocalOffsets after this curve's own end, distance-shifted to continue from
	 * Length(). Ported from URoadCurve::Join. Does not update Offsets -- call
	 * RecomputeEffectiveOffsets() afterward.
	 */
	void Join(URealtimeRoadCurve* Other);
};
