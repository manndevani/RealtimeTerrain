// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoadTypes.h"

namespace UE::Geometry { class FDynamicMesh3; }

class AActor;
class ATerraDyneManager;

/**
 * Shared geometry construction for roads and junctions.
 *
 * Everything the road system renders is a cross-section swept along a path, so both the spline
 * actor and the junction actor build their meshes through SweepSection. Keeping one implementation
 * means UVs, material IDs, winding and end caps only have to be correct once.
 *
 * Conventions:
 *  - A section is a polygon in a 2D (Lateral, Vertical) plane. Lateral runs along the frame's
 *    Right axis, Vertical along its Up axis.
 *  - Section vertices are ordered so that the traversal runs left-to-right across the top and
 *    right-to-left along the bottom. SweepSection's winding rule derives outward-facing normals
 *    from that ordering, so a section authored the other way round renders inside out.
 *  - A vertex carries the material for the quad spanning from itself to the NEXT vertex. Material
 *    is an edge property, not a vertex property; treating it as a vertex property is what produced
 *    the asymmetric curb materials in the previous implementation.
 */
namespace RoadMesh
{
	/** Material slots, matching the three prototype materials bound by the actors. */
	namespace Slots
	{
		static constexpr int32 Surface = 0;
		static constexpr int32 Curb = 1;
		static constexpr int32 Footpath = 2;
		/** Vertical flanks and the underside read as concrete, so they share the curb slot. */
		static constexpr int32 Flank = Curb;
		static constexpr int32 Count = 3;
	}

	/** One vertex of a cross-section, in the section's own (Lateral, Vertical) plane. */
	struct FSectionVertex
	{
		double Lateral = 0.0;
		double Vertical = 0.0;
		/** Material for the quad spanning from this vertex to the next one in the section. */
		int32 EdgeMaterialID = Slots::Surface;

		FSectionVertex() = default;
		FSectionVertex(double InLateral, double InVertical, int32 InEdgeMaterialID)
			: Lateral(InLateral), Vertical(InVertical), EdgeMaterialID(InEdgeMaterialID)
		{
		}
	};

	using FSection = TArray<FSectionVertex>;

	/** An orthonormal frame in world space at one station along a swept path. */
	struct FFrame
	{
		FVector Origin = FVector::ZeroVector;
		FVector Tangent = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		FVector Up = FVector::UpVector;
		/** Arc length from the start of the path, used for the V coordinate. */
		double DistanceAlong = 0.0;
	};

	struct FSweepOptions
	{
		/** World units to UV units. UV = world distance * UVScale, so texel density is uniform. */
		float UVScale = 0.002f;

		/** Whether the last section vertex wraps back to the first (a solid) or not (an open strip). */
		bool bSectionIsClosed = true;

		/** Cap the two ends of the sweep. Only meaningful for a closed section. */
		bool bCapStart = true;
		bool bCapEnd = true;

		/**
		 * Section vertices with |Lateral| beyond this are the scalable "outer band" -- curb and
		 * footpath, swept independently per station at a fixed Lateral -- rather than the core road
		 * surface, which never scales. 0 (default) disables band scaling entirely: every vertex
		 * sweeps at its authored Lateral unchanged, exactly as before this option existed.
		 *
		 * Fixes the same self-intersection a too-wide roadside band gets at a junction corner (see
		 * ARealtimeRoadJunctionActor::BuildBoundary), but along the body of a curve instead of at a
		 * corner: a band swept at a fixed Lateral independently at each station is a textbook
		 * per-vertex-normal offset, which bowties on the inside of any turn tighter than the band is
		 * wide. BandScalePerFrame is what narrows the band exactly where the turn demands it.
		 */
		double BandInnerHalfWidth = 0.0;

		/**
		 * Per frame (parallel to the Frames array passed to SweepSection), 0..1 scale applied to the
		 * portion of every outer-band vertex's Lateral beyond BandInnerHalfWidth. Empty (default, or
		 * BandInnerHalfWidth left at 0) means unscaled -- every station sweeps the full band width.
		 */
		TArray<float> BandScalePerFrame;
	};

	/** How much wider than the band a turn's local radius has to be before the band stays well formed. */
	constexpr double BandRadiusSafetyFactor = 1.35;

	/**
	 * Per frame, how much a roadside band up to RequestedBandWidth wide must shrink to avoid folding
	 * through the local turn -- 1 where the turn is gentle enough to carry the full width, down to 0
	 * at a turn tight enough to leave no room for any band at all. Mirrors the same
	 * MaxBandWidth/RequestedBand ratio ARealtimeRoadJunctionActor::RebuildRoad computes for a
	 * junction corner, but per station along a curve, from the local radius of curvature RMF-style
	 * frames already carry (consecutive stations' Tangent divided by the distance between them).
	 */
	void ComputeBandScaleFromCurvature(
		const TArray<FFrame>& Frames,
		double RequestedBandWidth,
		double SafetyFactor,
		TArray<float>& OutBandScale);

	/** Enables the attribute overlays this module writes: UVs, material IDs and triangle groups. */
	void PrepareMesh(UE::Geometry::FDynamicMesh3& Mesh);

	/**
	 * Builds the closed cross-section of a road slab: cambered drive surface, optional curbs and
	 * footpaths, vertical flanks and a full underside.
	 *
	 * Rough roads pass false for bIncludeKerbAndPath and get the bare cambered slab.
	 */
	void BuildRoadSection(
		const FRoadCrossSectionSettings& Settings,
		bool bIncludeKerbAndPath,
		FSection& OutSection);

	/**
	 * Builds the open cross-section of a roadside band: curb face, curb top, sidewalk and outer
	 * flank, with Lateral zero sitting on the junction boundary and increasing Lateral pointing
	 * away from the junction.
	 *
	 * The band deliberately has no inner wall. The junction slab's own skirt occupies that plane,
	 * and two coincident faces would z-fight.
	 */
	void BuildRoadsideSection(
		double CurbWidth,
		double CurbHeight,
		double FootpathWidth,
		double FootpathHeight,
		double Thickness,
		FSection& OutSection);

	/**
	 * Builds stable frames along a world-space polyline.
	 *
	 * Right is derived from world up rather than from a spline's own up vector, so the section
	 * stays vertical instead of rolling with the spline's arbitrary twist. Tangents come from
	 * central differences of the supplied points, which means a path whose Z has been rewritten to
	 * follow terrain produces frames that tilt with the actual slope.
	 *
	 * Returns false when fewer than two usable points were supplied.
	 */
	bool BuildFramesFromPolyline(const TArray<FVector>& Points, TArray<FFrame>& OutFrames);

	/** Rolls each frame about its tangent. Used for camber and terrain cross-slope. */
	void ApplyRoll(TArray<FFrame>& Frames, const TArray<double>& RollRadians);

	/**
	 * Sweeps Section along Frames, appending to Mesh.
	 *
	 * Vertices are computed in world space and then mapped through WorldToLocal, so callers can
	 * work entirely in world space regardless of where the component sits.
	 */
	void SweepSection(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FSection& Section,
		const TArray<FFrame>& Frames,
		const FTransform& WorldToLocal,
		const FSweepOptions& Options);

	/**
	 * Appends a solid prism: a horizontal top face over the given counter-clockwise world-space
	 * boundary, a matching bottom face at BaseZOffset below it, and the skirt joining them.
	 *
	 * Used for junction slabs, whose boundary is a filleted polygon rather than a swept path.
	 */
	void AppendPrism(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector>& BoundaryCCW,
		double BaseZOffset,
		int32 TopMaterialID,
		int32 SideMaterialID,
		const FTransform& WorldToLocal,
		float UVScale);

	/**
	 * Terrain height at a world XY: TerraDyne's heightfield where a chunk covers the spot, and a
	 * downward line trace otherwise.
	 *
	 * ManagerCache holds the resolved manager between calls. Road generation samples terrain once
	 * per station, so re-finding the manager per sample would mean sweeping every actor in the
	 * level hundreds of times per rebuild.
	 */
	bool SampleGroundHeight(
		const AActor* Context,
		TWeakObjectPtr<ATerraDyneManager>& ManagerCache,
		const FVector2D& WorldXY,
		float& OutHeightZ);

	/** Total length of a polyline. Zero for fewer than two points. */
	double PolylineLength(const TArray<FVector>& Points);

	/**
	 * Resamples a polyline to roughly uniform spacing, preserving the two endpoints.
	 * Returns the input unchanged when it is too short to resample.
	 */
	void ResamplePolyline(const TArray<FVector>& In, double TargetSpacing, TArray<FVector>& Out);

	/**
	 * Smooths the Z channel of a polyline with a moving average of the given half-window, leaving
	 * XY untouched. Terrain sampled per vertex is noisy enough to make a road visibly lumpy; this
	 * keeps the road following the landform without following every pebble.
	 */
	void SmoothHeights(TArray<FVector>& Points, int32 HalfWindow);
}
