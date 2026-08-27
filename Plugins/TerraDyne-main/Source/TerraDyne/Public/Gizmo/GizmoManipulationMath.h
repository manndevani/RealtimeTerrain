// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Gizmo/GizmoTypes.h"

/**
 * Pure manipulation math for the runtime transform gizmo -- no UObject/UWorld/UPrimitiveComponent
 * references anywhere in this module, so every function here is directly unit-testable without a
 * rendered viewport, a spawned actor, or even a World. Mirrors this project's own established
 * pattern for math modules (RoadCurveGraph, RoadElevationProfile, RoadSpiral, RoadPierPlacement,
 * RoadXodrExport all take the same shape: plain data in, plain data out).
 *
 * Every delta-computation function here is built on "initial transform + accumulated delta since
 * drag start," never "current transform + this frame's delta" -- the former is immune to floating-
 * point drift over a long drag; the latter compounds rounding error onto itself every frame.
 */
namespace GizmoMath
{
	/**
	 * Resolves a handle's primary axis to a world-space unit vector. World space always returns a
	 * world basis vector; Local space rotates it by TargetRotation. RotateScreen/Center/ScaleUniform
	 * have no single axis and return ZeroVector -- callers branch on handle type first.
	 */
	TERRADYNE_API FVector ResolveAxisDirection(EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation);

	/**
	 * Resolves a plane handle (PlaneXY/XZ/YZ) to its world-space plane normal -- the one axis NOT
	 * spanned by the plane (PlaneXY's normal is the Z axis, etc.), subject to the same World/Local
	 * resolution as ResolveAxisDirection. Returns ZeroVector for non-plane handles.
	 */
	TERRADYNE_API FVector ResolvePlaneNormal(EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation);

	/**
	 * For a translate drag, which plane to intersect the cursor ray against. Axis handles (X/Y/Z)
	 * use whichever of the two planes containing that axis best faces the camera -- ported from this
	 * project's existing BestFacingVerticalPlaneNormal fix (a horizontal plane degenerates when the
	 * camera looks straight down its normal), generalized beyond just the world-Z-axis special case
	 * both existing gizmo consumers hard-coded it for. Plane handles use their own resolved normal
	 * directly. Center uses the camera's own forward vector (a view-facing plane).
	 */
	TERRADYNE_API FPlane ResolveTranslateDragPlane(
		EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
		const FVector& GizmoOrigin, const FVector& CursorDir, const FVector& CameraForward);

	/**
	 * A vertical plane normal, chosen from the cursor ray's own horizontal projection rather than
	 * "camera-to-point" -- the latter degenerates to near-zero looking straight down. Ported
	 * verbatim from the two duplicated copies this project already had (TerraDyneEditController.cpp,
	 * RealtimeRoadBuilderComponent.cpp) -- now single-sourced.
	 */
	TERRADYNE_API FVector BestFacingVerticalPlaneNormal(const FVector& CursorDir);

	/**
	 * Ray/plane intersection that explicitly reports failure on a near-parallel ray (unlike
	 * FMath::RayPlaneIntersection alone, which returns a huge/garbage point rather than failing).
	 */
	TERRADYNE_API bool IntersectRayWithPlane(const FGizmoInteractionRay& Ray, const FPlane& Plane, FVector& OutIntersection);

	/**
	 * Translate delta since drag start, already resolved to World/Local space and constrained to
	 * the handle's own axis/plane -- the caller adds this directly to InitialTransform's location.
	 * Axis handles project (Current-Start) onto the resolved axis (so Local-space translate is
	 * actually constrained to the target's own rotated axis, not the world axis the intersection
	 * plane happened to be built from); plane/Center handles use the full in-plane delta.
	 */
	TERRADYNE_API FVector ComputeTranslateDelta(
		EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
		const FVector& StartIntersection, const FVector& CurrentIntersection);

	/**
	 * Signed rotation angle (radians) around Axis, from the angle StartIntersection makes around
	 * GizmoOrigin (projected into the plane perpendicular to Axis) to the angle CurrentIntersection
	 * makes -- the standard "arc drag" ring-rotation technique. Wrapped via
	 * FMath::FindDeltaAngleRadians (this project's own established angle-wrap convention, already
	 * used in RoadNetworkSolver.cpp and RoadSpiral.cpp) so a drag can never jump by more than PI in
	 * one evaluation regardless of how the two angles happen to wrap.
	 */
	TERRADYNE_API double ComputeRotationAngle(
		const FVector& Axis, const FVector& GizmoOrigin,
		const FVector& StartIntersection, const FVector& CurrentIntersection);

	/**
	 * Convenience wrapper: resolves Handle's rotation axis (RotateScreen uses CameraForward instead
	 * of a target-relative axis) and returns the full delta quaternion via ComputeRotationAngle.
	 */
	TERRADYNE_API FQuat ComputeRotationDelta(
		EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
		const FVector& GizmoOrigin, const FVector& CameraForward,
		const FVector& StartIntersection, const FVector& CurrentIntersection);

	/**
	 * Per-component scale multiplier (1,1,1 for an untouched axis) since drag start, as a ratio of
	 * the cursor's current distance from GizmoOrigin along the handle's axis to its distance at drag
	 * start -- multiplicative rather than additive so it behaves consistently at any zoom level and
	 * can never reach zero or negative (the ratio is clamped away from zero on both the numerator and
	 * denominator side). The caller multiplies this component-wise into InitialTransform's scale.
	 * ScaleUniform measures plain 3D distance from GizmoOrigin instead of a single axis and returns
	 * the same ratio in all three components.
	 */
	TERRADYNE_API FVector ComputeScaleDelta(
		EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
		const FVector& GizmoOrigin, const FVector& StartIntersection, const FVector& CurrentIntersection);

	/** Generic value snap -- the building block every mode's snapping reduces to. */
	TERRADYNE_API double SnapValue(double Value, double Increment);

	/** Componentwise SnapValue, for translate deltas and scale ratios. */
	TERRADYNE_API FVector SnapVector(const FVector& Value, double Increment);
}
