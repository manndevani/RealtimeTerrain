// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Euler-spiral (clothoid) centreline math, ported from RoadBuilder's Spiral.h/Spiral.cpp
 * (Publisher: Fullike, https://github.com/fullike) -- the same transition-curve math real highway
 * design uses to connect a straight run to a circular-arc corner without a curvature discontinuity
 * (a driver's steering wheel turns at a constant *rate* through the transition, not with a jerk at
 * the corner's tangent point).
 *
 * `GetSpiralPos`/`GetSpiralRadian`/`ComputeSpiralLength` are the ported primitives, near-verbatim
 * (see RoadSpiral.cpp for the Fresnel-integral numerics underneath). `BuildSpiralCentreline` is new
 * code built on top of them: it fits a spiral-in/arc/spiral-out transition at every interior control
 * point of a polyline (mirroring RoadBuilder's `ARoadActor::UpdateCurve` corner-fitting loop) and
 * resamples the whole thing into a dense point/distance array -- the same shape
 * `ARealtimeRoadSplineActor::BuildConformedCentreline` already produces via `USplineComponent`'s
 * own (curvature-discontinuous) interpolation, so it's a drop-in replacement for the XY component
 * of that pipeline. Only used in the settle pass; live-drag keeps the cheap spline sampling.
 */
namespace RoadSpiral
{
	/** Position at arc-length Length along a spiral starting at (InitX,InitY) heading InitTheta, with curvature InitCurv changing at rate DCurv per unit length. */
	REALTIMETERRAIN_API FVector2D GetSpiralPos(double InitX, double InitY, double InitTheta, double InitCurv, double DCurv, double Length);

	/** Heading (radians) at arc-length Length along the same spiral. */
	REALTIMETERRAIN_API double GetSpiralRadian(double InitTheta, double InitCurv, double DCurv, double Length);

	/** Arc-length a spiral needs to travel to turn its heading from Theta0 to Theta1 while its curvature ramps linearly from K0 to K1. */
	REALTIMETERRAIN_API double ComputeSpiralLength(double Theta0, double Theta1, double K0, double K1);

	/**
	 * Fits a curvature-continuous centreline through ControlPoints (a road's own spline points, XY
	 * only -- height is handled entirely separately by the existing terrain-conforming/elevation-
	 * profile pipeline, exactly as RoadBuilder's own height channel is independent of its planview
	 * curve). Straight runs connect consecutive points; at each interior point, a spiral-arc-spiral
	 * transition is fit so the path turns by the corner's full angle without a curvature jump,
	 * bisecting on curvature (mirroring RoadBuilder's own corner-fit loop) until the transition's
	 * midpoint lands on the corner's own position.
	 *
	 * MaxRadius caps how tight the fitted arc may get (a very sharp corner is bevelled by shortening
	 * the transition length instead of exceeding it). CurvatureBlend splits each corner's turn
	 * between its spiral portions and its constant-curvature arc portion: 0 is a pure circular-arc
	 * corner (no spiral, a curvature jump at both ends -- matching plain arc-filleted corners),
	 * 1 is a pure spiral corner (curvature ramps from 0 to a peak and back to 0, no constant-radius
	 * arc in the middle), matching RoadBuilder's own `RoadPoint.CurvatureBlend` semantics exactly.
	 *
	 * OutPoints/OutDistances are resampled at roughly SampleStep spacing, matching the shape
	 * BuildConformedCentreline's own spline-sampling loop produces. Leaves both arrays empty if
	 * ControlPoints has fewer than 2 points or the fitted length comes out to ~0.
	 */
	REALTIMETERRAIN_API void BuildSpiralCentreline(
		const TArray<FVector2D>& ControlPoints,
		double MaxRadius,
		double CurvatureBlend,
		double SampleStep,
		TArray<FVector2D>& OutPoints,
		TArray<double>& OutDistances);
}
