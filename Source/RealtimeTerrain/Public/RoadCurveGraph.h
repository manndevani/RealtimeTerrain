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
	 * Largest index i such that Elements[i].Dist <= Dist (binary search over a Dist-sorted array).
	 * Ported from RoadBuilder's GetElementIndex (RoadCurve.h), generalised to any element type
	 * with a public double Dist -- RoadBuilder itself reuses the identical template for its offset
	 * keys, lane segments and boundary segments alike.
	 */
	template <typename ElementType>
	int32 FindElementIndex(const TArray<ElementType>& Elements, double Dist)
	{
		int32 Length = Elements.Num();
		int32 Middle = Length;
		int32 Offset = 0;
		while (Middle > 0)
		{
			Middle = Length / 2;
			if (Elements[Offset + Middle].Dist <= Dist)
			{
				Offset += Middle;
			}
			Length -= Middle;
		}
		return Offset;
	}

	/**
	 * Index i such that Keys[i].Dist <= Dist, clamped so i+1 is always a valid bracketing key.
	 * Ported from RoadBuilder's GetPointIndex (RoadCurve.h), built on FindElementIndex above.
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

/** Mirrors RoadBuilder's ELaneType (LaneLane.h). */
UENUM()
enum class ERealtimeLaneType : uint8
{
	None,
	Driving,
	Shoulder,
	Sidewalk,
	Median,
	Collapsed,
};

/** One stretch of a lane's length carrying a given lane type. Mirrors RoadBuilder's FLaneSegment. */
USTRUCT()
struct FRealtimeLaneSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Segment)
	double Dist = 0.0;

	UPROPERTY(EditAnywhere, Category = Segment)
	ERealtimeLaneType LaneType = ERealtimeLaneType::Driving;
};

/** One stretch of a boundary's length carrying a marking. Mirrors RoadBuilder's FBoundarySegment. */
USTRUCT()
struct FRealtimeBoundarySegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Segment)
	double Dist = 0.0;

	/** True for a solid line (e.g. no-passing, outer edge); false for dashed/none. */
	UPROPERTY(EditAnywhere, Category = Segment)
	bool bSolidMarking = false;
};

class URealtimeRoadBoundary;

/**
 * Sits between two boundaries and describes what kind of lane occupies that gap along its
 * length. Mirrors RoadBuilder's URoadBuilderLane (RoadLane.h). A lane's own inherited
 * Offsets/LocalOffsets (from URealtimeRoadCurve) go unused, matching RoadBuilder: a lane's actual
 * shape comes from sweeping between LeftBoundary and RightBoundary, not from any curve data of
 * its own -- which is also why, unlike URealtimeRoadBoundary, Cut/Join here only touch Segments.
 */
UCLASS()
class REALTIMETERRAIN_API URealtimeRoadLane : public URealtimeRoadCurve
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Lane)
	TArray<FRealtimeLaneSegment> Segments;

	UPROPERTY()
	TObjectPtr<URealtimeRoadBoundary> LeftBoundary;

	UPROPERTY()
	TObjectPtr<URealtimeRoadBoundary> RightBoundary;

	double SegmentStart(int32 Index) const { return Segments[Index].Dist; }
	double SegmentEnd(int32 Index) const { return Index + 1 < Segments.Num() ? Segments[Index + 1].Dist : Length(); }

	/** Index of the segment covering Dist. Ported from URoadBuilderLane::GetSegment(double). */
	int32 GetSegmentAt(double Dist) const;

	/** Appends Lane's segments after this lane's own end. Ported from URoadBuilderLane::Join. */
	void Join(URealtimeRoadLane* Lane);

	/** Trims Segments to [Start, End] and re-bases distances. Ported from URoadBuilderLane::Cut. */
	void Cut(double Start, double End);
};

/**
 * A named edge -- a lane divider, a road edge, a sidewalk edge -- independently curvable via its
 * inherited offset profile. Mirrors RoadBuilder's URoadBoundary (RoadBoundary.h). Unlike a lane, a
 * boundary's own curve data is real and load-bearing (it is what BuildRoadSection samples to place
 * each lane's edge), so Cut/Join here trim/join both the curve (Offsets/LocalOffsets) and Segments.
 */
UCLASS()
class REALTIMETERRAIN_API URealtimeRoadBoundary : public URealtimeRoadCurve
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Boundary)
	TArray<FRealtimeBoundarySegment> Segments;

	UPROPERTY()
	TObjectPtr<URealtimeRoadLane> LeftLane;

	UPROPERTY()
	TObjectPtr<URealtimeRoadLane> RightLane;

	double SegmentStart(int32 Index) const { return Segments[Index].Dist; }
	double SegmentEnd(int32 Index) const { return Index + 1 < Segments.Num() ? Segments[Index + 1].Dist : Length(); }

	/** Appends Boundary's curve and segments after this boundary's own end. Ported from URoadBoundary::Join. */
	void Join(URealtimeRoadBoundary* Boundary);

	/** Trims the curve and Segments to [Start, End] and re-bases distances. Ported from URoadBoundary::Cut. */
	void Cut(double Start, double End);
};
