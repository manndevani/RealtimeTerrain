// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoadElevationProfile.h"

/**
 * Self-contained, editor-only .xodr (ASAM OpenDRIVE) export, adapted from RoadBuilder's ExportXodr
 * methods (RoadActor.cpp/RoadBoundary.cpp/RoadLane.cpp/RoadScene.cpp) to this project's own data
 * shapes. No runtime or replication cost -- this is called from an editor utility action on demand,
 * never during play.
 *
 * Deliberately scoped, not a full RoadBuilder-parity export -- documented here rather than silently
 * faked:
 *  - planView geometry is exported as a sequence of straight <line> records between the road's
 *    already-sampled centreline stations (whatever ARealtimeRoadSplineActor::BuildConformedCentreline
 *    last produced, spiral-fit or not). This is a standard, honest discretization for a spline-
 *    authored road -- most real-world OpenDRIVE exporters do the same for tools that don't hold an
 *    analytic curve representation end-to-end. It is not the exact analytic spiral/arc/line record
 *    RoadSpiral::BuildSpiralCentreline could in principle produce; wiring that through is a
 *    follow-on, not required for a correct, loadable .xodr today.
 *  - elevationProfile exports the road's *authored* HeightKeys verbatim, not the runtime auto-
 *    flyover bridges ApplyElevationProfile blends in -- auto-bridging depends on live terrain
 *    sampling and is a procedural convenience, not part of the road's portable design geometry.
 *  - lanes exports a single laneSection spanning the whole road (this project doesn't yet have
 *    lane-width keys that vary along a road's length), built from a style preset's lane list or a
 *    sensible CrossSectionSettings-derived default -- matching
 *    ARealtimeRoadSplineActor::GetDrivingLaneCount()'s own established fallback.
 *  - junctions are not exported as XODR <junction> elements, and junction-spanning roads are not
 *    split into per-junction-arm road records the way RoadBuilder's Slots/CutRoadSegments does --
 *    each road exports as one continuous <road>. Real per-lane connectivity through a junction
 *    (RealtimeRoadJunctionActor::SolvedConnectors) is a follow-on.
 *
 * The Hermite-to-power-basis conversion for elevation records (CalcElevationPolynomial) is a fresh
 * derivation, not RoadBuilder's own CalcABCD call site for elevation -- that call site passes its
 * Dir tangent without multiplying by the segment length, which its own FHeightSegment::Get (the
 * actively-used height evaluator) does do; cross-checking CalcABCD's algebra against Get's
 * FMath::CubicInterp convention shows CalcABCD expects an already length-scaled tangent, so the
 * elevation ExportXodr call site looks like an unexercised bug in the third-party plugin. This port
 * does the length-scaling correctly instead of reproducing that mismatch.
 */
namespace RoadXodrExport
{
	enum class ELaneExportType : uint8
	{
		Driving,
		Shoulder,
		Sidewalk,
		Median,
		None
	};

	struct FLaneExportEntry
	{
		ELaneExportType Type = ELaneExportType::Driving;
		double WidthCm = 0.0;
	};

	/** Everything needed to export one road as a single XODR <road> element. */
	struct FRoadExportData
	{
		FString Name;
		int32 Id = 0;

		/** World-space XY centreline samples (cm), exported as consecutive <line> geometry records. */
		TArray<FVector2D> CentrelineXY;
		/** Arc-length distance (cm) along the centreline, parallel to CentrelineXY. */
		TArray<double> Distances;

		/** Authored elevation keys (RoadElevationProfile.h), exported verbatim -- see class comment. */
		TArray<FRoadHeightKey> HeightKeys;

		/** Left-to-right-adjacent-to-centre lane lists, mirroring URoadStylePreset's shape. */
		TArray<FLaneExportEntry> LeftLanes;
		TArray<FLaneExportEntry> RightLanes;
	};

	/**
	 * Cubic Hermite-to-power-basis conversion for one elevation (or width) polynomial segment:
	 * given endpoint values P0/P1 and TRUE derivatives M0/M1 (dValue/dDistance, matching
	 * RoadElevationProfile::EvaluateHeight's Slope convention) over a segment of length
	 * SegmentLength (same units as P0/P1's distance axis), returns (A,B,C,D) such that
	 * f(ds) = A + B*ds + C*ds^2 + D*ds^3 for ds in [0, SegmentLength] reproduces P0/M0/P1/M1 at the
	 * two endpoints exactly. Exposed publicly so its correctness is directly testable.
	 */
	REALTIMETERRAIN_API FVector4 CalcElevationPolynomial(double P0, double M0, double P1, double M1, double SegmentLength);

	/** Builds one complete .xodr document (an OpenDRIVE header plus one <road> per entry) as a UTF-8 XML string. */
	REALTIMETERRAIN_API FString ExportOpenDrive(const TArray<FRoadExportData>& Roads);
}
