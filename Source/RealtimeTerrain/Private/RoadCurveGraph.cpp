// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadCurveGraph.h"

namespace RoadCurveGraph
{
	int32 FindBracketIndex(const TArray<FRoadCurveOffsetKey>& Keys, double Dist)
	{
		int32 Offset = FindElementIndex(Keys, Dist);
		// Ported from GetPointIndex: clamp so Offset+1 is always a valid bracketing key.
		if (Offset == Keys.Num() - 1)
		{
			Offset--;
		}
		return Offset;
	}

	double EvaluateOffset(const TArray<FRoadCurveOffsetKey>& Keys, double Dist)
	{
		const int32 Index = FindBracketIndex(Keys, Dist);
		const FRoadCurveOffsetKey& Start = Keys[Index];
		const FRoadCurveOffsetKey& End = Keys[Index + 1];
		const double SegmentLength = End.Dist - Start.Dist;
		const double S = Dist - Start.Dist;
		return FMath::CubicInterp(Start.Offset, Start.Slope * SegmentLength, End.Offset, End.Slope * SegmentLength, S / SegmentLength);
	}

	double EvaluateHeadingDelta(const TArray<FRoadCurveOffsetKey>& Keys, double Dist)
	{
		const int32 Index = FindBracketIndex(Keys, Dist);
		const FRoadCurveOffsetKey& Start = Keys[Index];
		const FRoadCurveOffsetKey& End = Keys[Index + 1];
		const double SegmentLength = End.Dist - Start.Dist;
		const double S = Dist - Start.Dist;
		const double Derivative = FMath::CubicInterpDerivative(Start.Offset, Start.Slope * SegmentLength, End.Offset, End.Slope * SegmentLength, S / SegmentLength);
		return FMath::Atan2(Derivative, SegmentLength);
	}

	TArray<FRoadCurveOffsetKey> CutOffsets(const TArray<FRoadCurveOffsetKey>& Keys, double Start, double End)
	{
		TArray<FRoadCurveOffsetKey> Results;
		for (int32 i = 0; i < Keys.Num() - 1; i++)
		{
			const FRoadCurveOffsetKey& ThisKey = Keys[i];
			const FRoadCurveOffsetKey& NextKey = Keys[i + 1];
			const double SegStart = FMath::Max(Start, ThisKey.Dist);
			const double SegEnd = FMath::Min(End, NextKey.Dist);
			if (SegStart < SegEnd)
			{
				const double LocalStart = SegStart - ThisKey.Dist;
				const double LocalEnd = SegEnd - ThisKey.Dist;
				const double SegmentLength = NextKey.Dist - ThisKey.Dist;
				check(SegmentLength > 0.0);

				FRoadCurveOffsetKey NewKey;
				NewKey.Dist = SegStart - Start;
				NewKey.Offset = FMath::CubicInterp(ThisKey.Offset, ThisKey.Slope * SegmentLength, NextKey.Offset, NextKey.Slope * SegmentLength, LocalStart / SegmentLength);
				NewKey.Slope = FMath::CubicInterpDerivative(ThisKey.Offset, ThisKey.Slope * SegmentLength, NextKey.Offset, NextKey.Slope * SegmentLength, LocalStart / SegmentLength) / SegmentLength;
				Results.Add(NewKey);

				if (SegEnd <= NextKey.Dist)
				{
					FRoadCurveOffsetKey LastKey;
					LastKey.Dist = SegEnd - Start;
					LastKey.Offset = FMath::CubicInterp(ThisKey.Offset, ThisKey.Slope * SegmentLength, NextKey.Offset, NextKey.Slope * SegmentLength, LocalEnd / SegmentLength);
					LastKey.Slope = FMath::CubicInterpDerivative(ThisKey.Offset, ThisKey.Slope * SegmentLength, NextKey.Offset, NextKey.Slope * SegmentLength, LocalEnd / SegmentLength) / SegmentLength;
					Results.Add(LastKey);
				}
			}
		}
		return Results;
	}

	void CleanOffsets(TArray<FRoadCurveOffsetKey>& Keys)
	{
		Keys.Sort();
		for (int32 i = 1; i < Keys.Num() - 1;)
		{
			const FRoadCurveOffsetKey& Prev = Keys[i - 1];
			const FRoadCurveOffsetKey& Next = Keys[i + 1];
			const FRoadCurveOffsetKey& Key = Keys[i];
			if (FMath::IsNearlyEqual(Prev.Dist, Key.Dist, 100.0) || FMath::IsNearlyEqual(Next.Dist, Key.Dist, 100.0))
			{
				Keys.RemoveAt(i);
				continue;
			}
			if (FMath::IsNearlyEqual(Prev.Offset, Key.Offset, 1.0) && FMath::IsNearlyEqual(Next.Offset, Key.Offset, 1.0))
			{
				Keys.RemoveAt(i);
				continue;
			}
			i++;
		}
	}
}

int32 URealtimeRoadCurve::AddLocalOffset(double Dist)
{
	const int32 Index = RoadCurveGraph::FindBracketIndex(LocalOffsets, Dist);
	FRoadCurveOffsetKey NewKey;
	NewKey.Dist = Dist;
	NewKey.Offset = LocalOffsets[Index].Offset;
	LocalOffsets.Insert(NewKey, Index + 1);
	return Index + 1;
}

int32 URealtimeRoadCurve::FindLocalOffset(double Dist, double Tolerance) const
{
	for (int32 i = 0; i < LocalOffsets.Num(); i++)
	{
		if (FMath::IsNearlyEqual(Dist, LocalOffsets[i].Dist, Tolerance))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void URealtimeRoadCurve::Cut(double Start, double End)
{
	if (FindLocalOffset(Start, 0.0) == INDEX_NONE)
	{
		AddLocalOffset(Start);
	}
	if (FindLocalOffset(End, 0.0) == INDEX_NONE)
	{
		AddLocalOffset(End);
	}
	for (int32 i = 0; i < LocalOffsets.Num();)
	{
		if (LocalOffsets[i].Dist < Start || LocalOffsets[i].Dist > End)
		{
			LocalOffsets.RemoveAt(i);
		}
		else
		{
			i++;
		}
	}
	for (FRoadCurveOffsetKey& Key : LocalOffsets)
	{
		Key.Dist -= Start;
	}
}

void URealtimeRoadCurve::Join(URealtimeRoadCurve* Other)
{
	LocalOffsets.Pop();
	const double ThisLength = Length();
	for (int32 i = 1; i < Other->LocalOffsets.Num(); i++)
	{
		FRoadCurveOffsetKey Key = Other->LocalOffsets[i];
		Key.Dist += ThisLength;
		LocalOffsets.Add(Key);
	}
}

int32 URealtimeRoadLane::GetSegmentAt(double Dist) const
{
	// Ported from URoadBuilderLane::GetSegment(double).
	return RoadCurveGraph::FindElementIndex(Segments, Dist);
}

void URealtimeRoadLane::Join(URealtimeRoadLane* Lane)
{
	const double ThisLength = Length();
	for (int32 i = 1; i < Lane->Segments.Num(); i++)
	{
		FRealtimeLaneSegment Segment = Lane->Segments[i];
		Segment.Dist += ThisLength;
		Segments.Add(Segment);
	}
}

void URealtimeRoadLane::Cut(double Start, double End)
{
	for (int32 i = 0; i < Segments.Num();)
	{
		const double SegStart = SegmentStart(i);
		const double SegEnd = SegmentEnd(i);
		if (SegEnd < Start || SegStart > End)
		{
			Segments.RemoveAt(i);
		}
		else
		{
			i++;
		}
	}
	for (FRealtimeLaneSegment& Segment : Segments)
	{
		Segment.Dist -= Start;
	}
}

void URealtimeRoadBoundary::Join(URealtimeRoadBoundary* Boundary)
{
	URealtimeRoadCurve::Join(Boundary);
	const double ThisLength = Length();
	for (int32 i = 1; i < Boundary->Segments.Num(); i++)
	{
		FRealtimeBoundarySegment Segment = Boundary->Segments[i];
		Segment.Dist += ThisLength;
		Segments.Add(Segment);
	}
}

void URealtimeRoadBoundary::Cut(double Start, double End)
{
	URealtimeRoadCurve::Cut(Start, End);
	for (int32 i = 0; i < Segments.Num();)
	{
		const double SegStart = SegmentStart(i);
		const double SegEnd = SegmentEnd(i);
		if (SegEnd < Start || SegStart > End)
		{
			Segments.RemoveAt(i);
		}
		else
		{
			i++;
		}
	}
	for (FRealtimeBoundarySegment& Segment : Segments)
	{
		Segment.Dist -= Start;
	}
}

namespace RoadCurveGraph
{
	void SerializeGraph(
		const TArray<URealtimeRoadBoundary*>& Boundaries,
		const TArray<URealtimeRoadLane*>& Lanes,
		TArray<FReplicatedBoundaryState>& OutBoundaryStates,
		TArray<FReplicatedLaneState>& OutLaneStates)
	{
		OutBoundaryStates.Reset(Boundaries.Num());
		OutLaneStates.Reset(Lanes.Num());

		for (URealtimeRoadBoundary* Boundary : Boundaries)
		{
			FReplicatedBoundaryState& State = OutBoundaryStates.AddDefaulted_GetRef();
			if (!Boundary)
			{
				continue;
			}
			State.LocalOffsets = Boundary->LocalOffsets;
			State.Segments = Boundary->Segments;
			State.LeftLaneIndex = Lanes.Find(Boundary->LeftLane);
			State.RightLaneIndex = Lanes.Find(Boundary->RightLane);
		}

		for (URealtimeRoadLane* Lane : Lanes)
		{
			FReplicatedLaneState& State = OutLaneStates.AddDefaulted_GetRef();
			if (!Lane)
			{
				continue;
			}
			State.Segments = Lane->Segments;
			State.LeftBoundaryIndex = Boundaries.Find(Lane->LeftBoundary);
			State.RightBoundaryIndex = Boundaries.Find(Lane->RightBoundary);
		}
	}

	void DeserializeGraph(
		UObject* Outer,
		const TArray<FReplicatedBoundaryState>& BoundaryStates,
		const TArray<FReplicatedLaneState>& LaneStates,
		TArray<URealtimeRoadBoundary*>& OutBoundaries,
		TArray<URealtimeRoadLane*>& OutLanes)
	{
		OutBoundaries.Reset(BoundaryStates.Num());
		OutLanes.Reset(LaneStates.Num());

		// First pass: create every object and copy its own data, before any cross-references are
		// wired up -- both arrays must exist in full before an index into either can resolve.
		for (const FReplicatedBoundaryState& State : BoundaryStates)
		{
			URealtimeRoadBoundary* Boundary = NewObject<URealtimeRoadBoundary>(Outer);
			Boundary->LocalOffsets = State.LocalOffsets;
			Boundary->Segments = State.Segments;
			Boundary->RecomputeEffectiveOffsets();
			OutBoundaries.Add(Boundary);
		}
		for (const FReplicatedLaneState& State : LaneStates)
		{
			URealtimeRoadLane* Lane = NewObject<URealtimeRoadLane>(Outer);
			Lane->Segments = State.Segments;
			OutLanes.Add(Lane);
		}

		// Second pass: resolve the index-based cross-references into real object pointers.
		for (int32 i = 0; i < BoundaryStates.Num(); i++)
		{
			const int32 LeftIndex = BoundaryStates[i].LeftLaneIndex;
			const int32 RightIndex = BoundaryStates[i].RightLaneIndex;
			OutBoundaries[i]->LeftLane = OutLanes.IsValidIndex(LeftIndex) ? OutLanes[LeftIndex] : nullptr;
			OutBoundaries[i]->RightLane = OutLanes.IsValidIndex(RightIndex) ? OutLanes[RightIndex] : nullptr;
		}
		for (int32 i = 0; i < LaneStates.Num(); i++)
		{
			const int32 LeftIndex = LaneStates[i].LeftBoundaryIndex;
			const int32 RightIndex = LaneStates[i].RightBoundaryIndex;
			OutLanes[i]->LeftBoundary = OutBoundaries.IsValidIndex(LeftIndex) ? OutBoundaries[LeftIndex] : nullptr;
			OutLanes[i]->RightBoundary = OutBoundaries.IsValidIndex(RightIndex) ? OutBoundaries[RightIndex] : nullptr;
		}
	}
}
