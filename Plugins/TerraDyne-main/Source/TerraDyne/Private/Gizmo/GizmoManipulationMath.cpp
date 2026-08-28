// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Gizmo/GizmoManipulationMath.h"

FVector GizmoMath::ResolveAxisDirection(EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation)
{
	FVector WorldAxis;
	switch (Handle)
	{
	case EGizmoHandleType::TranslateX:
	case EGizmoHandleType::RotateX:
	case EGizmoHandleType::ScaleX:
		WorldAxis = FVector::XAxisVector;
		break;
	case EGizmoHandleType::TranslateY:
	case EGizmoHandleType::RotateY:
	case EGizmoHandleType::ScaleY:
		WorldAxis = FVector::YAxisVector;
		break;
	case EGizmoHandleType::TranslateZ:
	case EGizmoHandleType::RotateZ:
	case EGizmoHandleType::ScaleZ:
		WorldAxis = FVector::ZAxisVector;
		break;
	default:
		return FVector::ZeroVector;
	}
	return (Space == EGizmoCoordinateSpace::Local) ? TargetRotation.RotateVector(WorldAxis) : WorldAxis;
}

FVector GizmoMath::ResolvePlaneNormal(EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation)
{
	FVector WorldNormal;
	switch (Handle)
	{
	case EGizmoHandleType::PlaneXY: WorldNormal = FVector::ZAxisVector; break;
	case EGizmoHandleType::PlaneXZ: WorldNormal = FVector::YAxisVector; break;
	case EGizmoHandleType::PlaneYZ: WorldNormal = FVector::XAxisVector; break;
	default: return FVector::ZeroVector;
	}
	return (Space == EGizmoCoordinateSpace::Local) ? TargetRotation.RotateVector(WorldNormal) : WorldNormal;
}

FVector GizmoMath::BestFacingVerticalPlaneNormal(const FVector& CursorDir)
{
	// Ported from this project's own TerraDyneEditController.cpp/RealtimeRoadBuilderComponent.cpp
	// (previously duplicated verbatim in both): among every plane containing a given vertical line,
	// the one whose normal is the cursor ray's own horizontal projection is the one the ray hits
	// most head-on. The alternative, "camera position minus the point," shrinks toward zero-length
	// (making its direction numerically arbitrary) whenever the camera sits near-overhead.
	const FVector Horizontal(CursorDir.X, CursorDir.Y, 0.0);
	if (!Horizontal.IsNearlyZero())
	{
		return Horizontal.GetSafeNormal();
	}
	return FVector::ForwardVector;
}

FPlane GizmoMath::ResolveAxisDragPlane(const FVector& Axis, const FVector& GizmoOrigin, const FVector& CursorDir, const FVector& CameraForward)
{
	// Generalizes BestFacingVerticalPlaneNormal from "the vertical axis specifically" (the only case
	// this project's existing code handled) to any axis: among every plane containing this axis's
	// line, use the one whose normal is CursorDir's own component perpendicular to the axis -- the
	// best-conditioned choice for any axis, not just a hard-coded vertical one. Shared by every
	// single-axis-constrained handle -- translate, scale, and rotate axes alike.
	FVector Normal = CursorDir - Axis * FVector::DotProduct(CursorDir, Axis);
	if (Normal.IsNearlyZero())
	{
		// The camera looks almost exactly along the axis being dragged -- no plane containing this
		// axis can be well-conditioned against this particular ray. Fall back to a plane built from
		// the camera's own forward vector, then world Up, then world Right, each only reached if the
		// previous one was itself degenerate against this exact axis.
		Normal = FVector::CrossProduct(Axis, CameraForward);
		if (Normal.IsNearlyZero())
		{
			Normal = FVector::CrossProduct(Axis, FVector::UpVector);
		}
		if (Normal.IsNearlyZero())
		{
			Normal = FVector::CrossProduct(Axis, FVector::RightVector);
		}
	}
	return FPlane(GizmoOrigin, Normal.GetSafeNormal());
}

FPlane GizmoMath::ResolveTranslateDragPlane(
	EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
	const FVector& GizmoOrigin, const FVector& CursorDir, const FVector& CameraForward)
{
	switch (Handle)
	{
	case EGizmoHandleType::TranslateX:
	case EGizmoHandleType::TranslateY:
	case EGizmoHandleType::TranslateZ:
		return ResolveAxisDragPlane(ResolveAxisDirection(Handle, Space, TargetRotation), GizmoOrigin, CursorDir, CameraForward);
	case EGizmoHandleType::PlaneXY:
	case EGizmoHandleType::PlaneXZ:
	case EGizmoHandleType::PlaneYZ:
		return FPlane(GizmoOrigin, ResolvePlaneNormal(Handle, Space, TargetRotation));
	case EGizmoHandleType::Center:
	{
		const FVector Normal = CameraForward.IsNearlyZero() ? FVector::UpVector : CameraForward.GetSafeNormal();
		return FPlane(GizmoOrigin, Normal);
	}
	default:
		return FPlane(GizmoOrigin, FVector::UpVector);
	}
}

bool GizmoMath::IntersectRayWithPlane(const FGizmoInteractionRay& Ray, const FPlane& Plane, FVector& OutIntersection)
{
	const FVector Normal(Plane.X, Plane.Y, Plane.Z);
	const double Denom = FVector::DotProduct(Ray.Direction, Normal);
	if (FMath::Abs(Denom) < UE_KINDA_SMALL_NUMBER)
	{
		// Ray is (near-)parallel to the plane -- there is no well-defined single intersection point.
		return false;
	}
	OutIntersection = FMath::RayPlaneIntersection(Ray.Origin, Ray.Direction, Plane);
	return true;
}

FVector GizmoMath::ComputeTranslateDelta(
	EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
	const FVector& StartIntersection, const FVector& CurrentIntersection)
{
	const FVector RawDelta = CurrentIntersection - StartIntersection;
	switch (Handle)
	{
	case EGizmoHandleType::TranslateX:
	case EGizmoHandleType::TranslateY:
	case EGizmoHandleType::TranslateZ:
	{
		const FVector Axis = ResolveAxisDirection(Handle, Space, TargetRotation);
		return Axis * FVector::DotProduct(RawDelta, Axis);
	}
	case EGizmoHandleType::PlaneXY:
	case EGizmoHandleType::PlaneXZ:
	case EGizmoHandleType::PlaneYZ:
	{
		// Removes any out-of-plane component explicitly rather than trusting the intersection alone
		// stayed exactly in-plane -- guards against float drift, not just the common case.
		const FVector Normal = ResolvePlaneNormal(Handle, Space, TargetRotation);
		return RawDelta - Normal * FVector::DotProduct(RawDelta, Normal);
	}
	case EGizmoHandleType::Center:
		return RawDelta;
	default:
		return FVector::ZeroVector;
	}
}

double GizmoMath::ComputeRotationAngle(
	const FVector& Axis, const FVector& GizmoOrigin,
	const FVector& StartIntersection, const FVector& CurrentIntersection)
{
	FVector Basis1 = FVector::CrossProduct(Axis, FVector::UpVector);
	if (Basis1.IsNearlyZero())
	{
		Basis1 = FVector::CrossProduct(Axis, FVector::RightVector);
	}
	if (Basis1.IsNearlyZero())
	{
		return 0.0;
	}
	Basis1 = Basis1.GetSafeNormal();
	const FVector Basis2 = FVector::CrossProduct(Axis, Basis1).GetSafeNormal();

	const FVector StartVec = StartIntersection - GizmoOrigin;
	const FVector CurrentVec = CurrentIntersection - GizmoOrigin;
	if (StartVec.IsNearlyZero() || CurrentVec.IsNearlyZero())
	{
		return 0.0;
	}

	const double StartAngle = FMath::Atan2(FVector::DotProduct(StartVec, Basis2), FVector::DotProduct(StartVec, Basis1));
	const double CurrentAngle = FMath::Atan2(FVector::DotProduct(CurrentVec, Basis2), FVector::DotProduct(CurrentVec, Basis1));

	// This project's own established angle-wrap convention (RoadNetworkSolver.cpp, RoadSpiral.cpp)
	// -- guarantees the returned delta is the shortest signed rotation, never a same-direction jump
	// larger than PI just because the two raw angles happened to wrap on opposite sides of +/-PI.
	return FMath::FindDeltaAngleRadians(StartAngle, CurrentAngle);
}

FQuat GizmoMath::ComputeRotationDelta(
	EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
	const FVector& GizmoOrigin, const FVector& CameraForward,
	const FVector& StartIntersection, const FVector& CurrentIntersection)
{
	FVector Axis;
	if (Handle == EGizmoHandleType::RotateScreen)
	{
		Axis = CameraForward.GetSafeNormal();
	}
	else
	{
		Axis = ResolveAxisDirection(Handle, Space, TargetRotation);
	}
	if (Axis.IsNearlyZero())
	{
		return FQuat::Identity;
	}

	const double Angle = ComputeRotationAngle(Axis, GizmoOrigin, StartIntersection, CurrentIntersection);
	return FQuat(Axis, Angle);
}

FVector GizmoMath::ComputeScaleDelta(
	EGizmoHandleType Handle, EGizmoCoordinateSpace Space, const FQuat& TargetRotation,
	const FVector& GizmoOrigin, const FVector& StartIntersection, const FVector& CurrentIntersection)
{
	constexpr double MinRatio = 0.01;
	constexpr double StandardReferenceDistance = 100.0; // Standard reference baseline (100 uu = 1m)

	if (Handle == EGizmoHandleType::ScaleUniform)
	{
		const double StartDist = FVector::Dist(StartIntersection, GizmoOrigin);
		const double CurrentDist = FVector::Dist(CurrentIntersection, GizmoOrigin);
		const double Delta = CurrentDist - StartDist;
		const double RefDist = FMath::Max(StandardReferenceDistance, StartDist);
		const double Ratio = FMath::Max(MinRatio, 1.0 + (Delta / RefDist));
		return FVector(Ratio, Ratio, Ratio);
	}

	int32 Component = -1;
	switch (Handle)
	{
	case EGizmoHandleType::ScaleX: Component = 0; break;
	case EGizmoHandleType::ScaleY: Component = 1; break;
	case EGizmoHandleType::ScaleZ: Component = 2; break;
	default: return FVector::OneVector;
	}

	const FVector Axis = ResolveAxisDirection(Handle, Space, TargetRotation);
	if (Axis.IsNearlyZero())
	{
		return FVector::OneVector;
	}

	const double StartOffset = FVector::DotProduct(StartIntersection - GizmoOrigin, Axis);
	const double CurrentOffset = FVector::DotProduct(CurrentIntersection - GizmoOrigin, Axis);
	const double Delta = CurrentOffset - StartOffset;
	const double RefDist = FMath::Max(StandardReferenceDistance, FMath::Abs(StartOffset));
	const double Ratio = FMath::Max(MinRatio, 1.0 + (Delta / RefDist));

	FVector Result = FVector::OneVector;
	Result[Component] = Ratio;
	return Result;
}

double GizmoMath::SnapValue(double Value, double Increment)
{
	if (Increment <= 0.0)
	{
		return Value;
	}
	return FMath::RoundToDouble(Value / Increment) * Increment;
}

FVector GizmoMath::SnapVector(const FVector& Value, double Increment)
{
	return FVector(SnapValue(Value.X, Increment), SnapValue(Value.Y, Increment), SnapValue(Value.Z, Increment));
}

double GizmoMath::DistanceRayToSegment(
	const FGizmoInteractionRay& Ray, const FVector& SegmentStart, const FVector& SegmentDir,
	double SegmentLength, double& OutDistanceAlongRay)
{
	// Standard closest-point-between-two-lines: line 1 is the ray (Origin + s*Direction), line 2 is
	// the segment's own infinite line (SegmentStart + t*SegmentDir). Both direction vectors are
	// assumed unit (Ray.Direction always is by convention in this module; SegmentDir is documented
	// as such on the header).
	const FVector& D1 = Ray.Direction;
	const FVector& D2 = SegmentDir;
	const FVector W0 = Ray.Origin - SegmentStart;

	const double A = FVector::DotProduct(D1, D1);
	const double B = FVector::DotProduct(D1, D2);
	const double C = FVector::DotProduct(D2, D2);
	const double D = FVector::DotProduct(D1, W0);
	const double E = FVector::DotProduct(D2, W0);
	const double Denom = A * C - B * B;

	double T;
	if (FMath::Abs(Denom) < UE_KINDA_SMALL_NUMBER)
	{
		// Parallel lines -- every point on the ray is equidistant from the segment's line in the
		// direction that matters here, so just solve directly for T.
		T = (C > UE_KINDA_SMALL_NUMBER) ? (E / C) : 0.0;
	}
	else
	{
		T = (A * E - B * D) / Denom;
	}

	T = FMath::Clamp(T, 0.0, SegmentLength);
	const FVector SegPoint = SegmentStart + D2 * T;

	// Distance from the (now fixed) segment point to the ray's own infinite line -- a small,
	// standard simplification versus a fully rigorous segment-segment closest point, sufficient for
	// an interactive hit test.
	const double AlongRay = FVector::DotProduct(SegPoint - Ray.Origin, D1);
	OutDistanceAlongRay = AlongRay;
	const FVector ClosestOnRay = Ray.Origin + D1 * AlongRay;
	return FVector::Dist(SegPoint, ClosestOnRay);
}

double GizmoMath::DistanceRayToPoint(const FGizmoInteractionRay& Ray, const FVector& Point, double& OutDistanceAlongRay)
{
	const double AlongRay = FVector::DotProduct(Point - Ray.Origin, Ray.Direction);
	OutDistanceAlongRay = AlongRay;
	const FVector ClosestOnRay = Ray.Origin + Ray.Direction * AlongRay;
	return FVector::Dist(Point, ClosestOnRay);
}

double GizmoMath::DistanceRayToRing(
	const FGizmoInteractionRay& Ray, const FVector& Origin, const FVector& Axis, double Radius,
	double& OutDistanceAlongRay)
{
	const FPlane RingPlane(Origin, Axis);
	FVector Intersection;
	if (!IntersectRayWithPlane(Ray, RingPlane, Intersection))
	{
		OutDistanceAlongRay = TNumericLimits<double>::Max();
		return TNumericLimits<double>::Max();
	}

	OutDistanceAlongRay = FVector::DotProduct(Intersection - Ray.Origin, Ray.Direction);
	const double RadialDist = FVector::Dist(Intersection, Origin);
	return FMath::Abs(RadialDist - Radius);
}

double GizmoMath::DistanceRayToQuad(
	const FGizmoInteractionRay& Ray, const FVector& Center, const FVector& AxisU, const FVector& AxisV,
	double HalfSize, double& OutDistanceAlongRay)
{
	const FVector Normal = FVector::CrossProduct(AxisU, AxisV).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		OutDistanceAlongRay = TNumericLimits<double>::Max();
		return TNumericLimits<double>::Max();
	}

	const FPlane QuadPlane(Center, Normal);
	FVector Intersection;
	if (!IntersectRayWithPlane(Ray, QuadPlane, Intersection))
	{
		OutDistanceAlongRay = TNumericLimits<double>::Max();
		return TNumericLimits<double>::Max();
	}

	OutDistanceAlongRay = FVector::DotProduct(Intersection - Ray.Origin, Ray.Direction);

	const FVector Local = Intersection - Center;
	const double U = FVector::DotProduct(Local, AxisU);
	const double V = FVector::DotProduct(Local, AxisV);

	const double DU = FMath::Max(0.0, FMath::Abs(U) - HalfSize);
	const double DV = FMath::Max(0.0, FMath::Abs(V) - HalfSize);
	return FMath::Sqrt(DU * DU + DV * DV);
}
