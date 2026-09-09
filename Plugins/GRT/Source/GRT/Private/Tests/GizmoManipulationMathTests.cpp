// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Gizmo/GizmoManipulationMath.h"

// Verifies ResolveAxisDirection/ResolvePlaneNormal: World space always returns a world basis
// vector regardless of the target's rotation; Local space rotates that basis vector by the
// target's own rotation -- the property that makes Local-space translate/rotate/scale actually
// follow a rotated object instead of staying world-aligned.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoAxisResolutionTest, "GRT.Gizmo.AxisResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoAxisResolutionTest::RunTest(const FString& Parameters)
{
	const FQuat Yaw90(FVector::UpVector, UE_DOUBLE_HALF_PI);

	{
		const FVector WorldX = GizmoMath::ResolveAxisDirection(EGizmoHandleType::TranslateX, EGizmoCoordinateSpace::World, Yaw90);
		TestTrue(TEXT("World space ignores target rotation"), WorldX.Equals(FVector::XAxisVector, 0.001));
	}
	{
		const FVector LocalX = GizmoMath::ResolveAxisDirection(EGizmoHandleType::TranslateX, EGizmoCoordinateSpace::Local, Yaw90);
		// A 90 degree yaw rotates world X onto world Y.
		TestTrue(TEXT("Local space rotates the axis with the target"), LocalX.Equals(FVector::YAxisVector, 0.01));
	}
	{
		const FVector PlaneXYNormal = GizmoMath::ResolvePlaneNormal(EGizmoHandleType::PlaneXY, EGizmoCoordinateSpace::World, FQuat::Identity);
		TestTrue(TEXT("PlaneXY's normal is world Z"), PlaneXYNormal.Equals(FVector::ZAxisVector, 0.001));
	}
	{
		const FVector NonAxisHandle = GizmoMath::ResolveAxisDirection(EGizmoHandleType::ScaleUniform, EGizmoCoordinateSpace::World, FQuat::Identity);
		TestTrue(TEXT("A handle with no single axis resolves to zero"), NonAxisHandle.IsNearlyZero());
	}
	return true;
}

// Verifies ComputeTranslateDelta's actual constraint behavior: an axis handle projects an
// arbitrary (non-axis-aligned) raw movement onto just its own axis; a plane handle removes
// whatever component of the raw movement falls outside its plane; Center passes the raw movement
// through untouched (it IS the constraint plane already).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoTranslateDeltaTest, "GRT.Gizmo.TranslateDelta", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoTranslateDeltaTest::RunTest(const FString& Parameters)
{
	const FVector Start(0.0, 0.0, 0.0);
	const FVector Current(300.0, 400.0, 500.0); // deliberately not axis-aligned

	{
		const FVector Delta = GizmoMath::ComputeTranslateDelta(EGizmoHandleType::TranslateX, EGizmoCoordinateSpace::World, FQuat::Identity, Start, Current);
		TestEqual(TEXT("TranslateX only moves along X"), Delta, FVector(300.0, 0.0, 0.0));
	}
	{
		const FVector Delta = GizmoMath::ComputeTranslateDelta(EGizmoHandleType::TranslateZ, EGizmoCoordinateSpace::World, FQuat::Identity, Start, Current);
		TestEqual(TEXT("TranslateZ only moves along Z"), Delta, FVector(0.0, 0.0, 500.0));
	}
	{
		const FVector Delta = GizmoMath::ComputeTranslateDelta(EGizmoHandleType::PlaneXY, EGizmoCoordinateSpace::World, FQuat::Identity, Start, Current);
		TestEqual(TEXT("PlaneXY keeps X/Y, drops Z"), Delta, FVector(300.0, 400.0, 0.0));
	}
	{
		const FVector Delta = GizmoMath::ComputeTranslateDelta(EGizmoHandleType::Center, EGizmoCoordinateSpace::World, FQuat::Identity, Start, Current);
		TestEqual(TEXT("Center passes the full raw delta through"), Delta, Current - Start);
	}
	{
		// Zero movement must produce exactly zero delta -- the pure-math analog of "cancel restores
		// the initial transform exactly" (a cancelled drag is one where Current never left Start).
		const FVector Delta = GizmoMath::ComputeTranslateDelta(EGizmoHandleType::TranslateX, EGizmoCoordinateSpace::World, FQuat::Identity, Start, Start);
		TestTrue(TEXT("No movement yields exactly zero delta"), Delta.IsNearlyZero());
	}
	return true;
}

// Verifies ComputeRotationAngle's arc-angle technique against a known quarter turn, confirms
// RotateScreen rotates around the camera's own forward vector rather than any target-relative
// axis, and confirms the angle is translation-invariant (still correct far from the world origin,
// standing in for "extreme camera distance" -- large-magnitude coordinates are where naive
// formulas tend to lose precision first).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoRotationAngleTest, "GRT.Gizmo.RotationAngle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoRotationAngleTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, 0.0);
	const FVector Axis = FVector::ZAxisVector;
	const FVector StartPoint(100.0, 0.0, 0.0);   // 0 degrees
	const FVector QuarterTurnPoint(0.0, 100.0, 0.0); // 90 degrees around +Z

	{
		const double Angle = GizmoMath::ComputeRotationAngle(Axis, Origin, StartPoint, QuarterTurnPoint);
		AddInfo(FString::Printf(TEXT("Quarter-turn angle: %.4f radians (expected +/- PI/2)"), Angle));
		TestEqual(TEXT("A quarter turn around Z measures PI/2"), FMath::Abs(Angle), UE_DOUBLE_HALF_PI, 0.001);
	}
	{
		const double NoMove = GizmoMath::ComputeRotationAngle(Axis, Origin, StartPoint, StartPoint);
		TestEqual(TEXT("No movement yields exactly zero rotation"), NoMove, 0.0, 1.0e-9);
	}
	{
		const FVector CameraForward(0.0, 0.0, -1.0); // looking straight down
		const FQuat Delta = GizmoMath::ComputeRotationDelta(
			EGizmoHandleType::RotateScreen, EGizmoCoordinateSpace::World, FQuat::Identity,
			Origin, CameraForward, StartPoint, QuarterTurnPoint);
		const FVector RotatedX = Delta.RotateVector(FVector::XAxisVector);
		AddInfo(FString::Printf(TEXT("RotateScreen rotated +X to (%.3f,%.3f,%.3f)"), RotatedX.X, RotatedX.Y, RotatedX.Z));
		// Rotating around -Z by the same quarter turn should carry +X toward -Y or +Y depending on
		// winding -- either way it must leave the XY plane's Z component at zero (rotation is
		// exactly around the camera's forward axis, not some other axis).
		TestEqual(TEXT("RotateScreen's rotation stays around the camera axis (no Z component introduced)"), RotatedX.Z, 0.0, 0.001);
	}
	{
		// Same quarter-turn geometry, translated one million cm from the origin -- the angle is a
		// function of directions relative to GizmoOrigin, so it must come out identical.
		const FVector FarOrigin(1.0e6, 1.0e6, 1.0e6);
		const double FarAngle = GizmoMath::ComputeRotationAngle(Axis, FarOrigin, FarOrigin + StartPoint, FarOrigin + QuarterTurnPoint);
		TestEqual(TEXT("Rotation angle is unaffected by being far from the world origin"), FMath::Abs(FarAngle), UE_DOUBLE_HALF_PI, 0.001);
	}
	return true;
}

// Verifies ComputeScaleDelta: uniform scale measures plain distance ratio in all three
// components; an axis handle affects only its own component; dragging back past the gizmo origin
// clamps to a small positive minimum rather than ever going to zero or negative.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoScaleDeltaTest, "GRT.Gizmo.ScaleDelta", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoScaleDeltaTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, 0.0);

	{
		const FVector Start(100.0, 0.0, 0.0);
		const FVector Current(200.0, 0.0, 0.0);
		const FVector Scale = GizmoMath::ComputeScaleDelta(EGizmoHandleType::ScaleUniform, EGizmoCoordinateSpace::World, FQuat::Identity, Origin, Start, Current);
		TestEqual(TEXT("Uniform scale doubles all three components"), Scale, FVector(2.0, 2.0, 2.0));
	}
	{
		const FVector Start(0.0, 100.0, 0.0);
		const FVector Current(0.0, 50.0, 0.0);
		const FVector Scale = GizmoMath::ComputeScaleDelta(EGizmoHandleType::ScaleY, EGizmoCoordinateSpace::World, FQuat::Identity, Origin, Start, Current);
		TestEqual(TEXT("ScaleY halves only the Y component"), Scale, FVector(1.0, 0.5, 1.0));
	}
	{
		// Dragging from +100 on the axis back past the origin to -20 must not produce a negative or
		// zero ratio.
		const FVector Start(0.0, 0.0, 100.0);
		const FVector Current(0.0, 0.0, -20.0);
		const FVector Scale = GizmoMath::ComputeScaleDelta(EGizmoHandleType::ScaleZ, EGizmoCoordinateSpace::World, FQuat::Identity, Origin, Start, Current);
		AddInfo(FString::Printf(TEXT("Scale after crossing the origin: %.4f"), Scale.Z));
		TestTrue(TEXT("Scale ratio never goes to zero or negative"), Scale.Z > 0.0);
	}
	{
		const FVector Start(50.0, 0.0, 0.0);
		const FVector Scale = GizmoMath::ComputeScaleDelta(EGizmoHandleType::ScaleX, EGizmoCoordinateSpace::World, FQuat::Identity, Origin, Start, Start);
		TestEqual(TEXT("No movement yields exactly a 1.0 ratio"), Scale.X, 1.0, 1.0e-9);
	}
	return true;
}

// Verifies BestFacingVerticalPlaneNormal's degenerate fallback, IntersectRayWithPlane's explicit
// near-parallel failure, and ResolveTranslateDragPlane's full fallback chain when the camera looks
// almost exactly down the axis being dragged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoPlaneResolutionTest, "GRT.Gizmo.PlaneResolutionAndRayIntersect", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoPlaneResolutionTest::RunTest(const FString& Parameters)
{
	{
		const FVector Normal = GizmoMath::BestFacingVerticalPlaneNormal(FVector(0.0, 0.0, -1.0));
		TestTrue(TEXT("Looking straight down falls back to ForwardVector"), Normal.Equals(FVector::ForwardVector, 0.001));
	}
	{
		FGizmoInteractionRay Ray;
		Ray.Origin = FVector(0.0, 0.0, 500.0);
		Ray.Direction = FVector(1.0, 0.0, 0.0);
		const FPlane HorizontalPlane(FVector::ZeroVector, FVector::UpVector);
		FVector Intersection;
		TestFalse(TEXT("A ray parallel to the plane reports no intersection"), GizmoMath::IntersectRayWithPlane(Ray, HorizontalPlane, Intersection));
	}
	{
		FGizmoInteractionRay Ray;
		Ray.Origin = FVector(0.0, 0.0, 500.0);
		Ray.Direction = FVector(0.0, 0.0, -1.0);
		const FPlane HorizontalPlane(FVector::ZeroVector, FVector::UpVector);
		FVector Intersection;
		TestTrue(TEXT("A ray hitting the plane head-on succeeds"), GizmoMath::IntersectRayWithPlane(Ray, HorizontalPlane, Intersection));
		TestEqual(TEXT("Intersection lands where expected"), Intersection, FVector::ZeroVector);
	}
	{
		// Camera looks straight down the Z axis while Z is the axis being dragged -- the primary
		// (cursor-perpendicular) plane normal degenerates to zero, and the CameraForward fallback
		// also degenerates (CameraForward is parallel to Axis too), so this must reach the UpVector
		// or RightVector fallback rather than returning a zero-length/NaN plane normal.
		const FVector Plane = GizmoMath::ResolveTranslateDragPlane(
			EGizmoHandleType::TranslateZ, EGizmoCoordinateSpace::World, FQuat::Identity,
			FVector::ZeroVector, FVector(0.0, 0.0, -1.0), FVector(0.0, 0.0, -1.0));
		TestTrue(TEXT("Fully degenerate camera/axis alignment still yields a valid, non-zero, finite plane normal"),
			!Plane.IsNearlyZero() && !Plane.ContainsNaN());
	}
	return true;
}

// Verifies the generic snapping building blocks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoSnappingTest, "GRT.Gizmo.Snapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoSnappingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("134 snaps to 100 at a 100 increment"), GizmoMath::SnapValue(134.0, 100.0), 100.0, 1.0e-9);
	TestEqual(TEXT("167 snaps to 200 at a 100 increment"), GizmoMath::SnapValue(167.0, 100.0), 200.0, 1.0e-9);
	TestEqual(TEXT("Zero or negative increment leaves the value untouched"), GizmoMath::SnapValue(123.456, 0.0), 123.456, 1.0e-9);

	const FVector Snapped = GizmoMath::SnapVector(FVector(134.0, 167.0, -49.0), 100.0);
	TestEqual(TEXT("SnapVector rounds every component independently"), Snapped, FVector(100.0, 200.0, -0.0));
	return true;
}

// Verifies DistanceRayToSegment against hand-computed cases: a ray straight down through the exact
// start of an X-axis segment (a perfect hit, distance 0, at the known travel distance); the same
// ray/segment pair offset 50 units sideways (a clean miss, distance exactly 50 -- confirming the
// segment-clamped closest point, not just raw line-line distance, drives the result).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoDistanceRayToSegmentTest, "GRT.Gizmo.DistanceRayToSegment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoDistanceRayToSegmentTest::RunTest(const FString& Parameters)
{
	FGizmoInteractionRay Ray;
	Ray.Origin = FVector(0.0, 0.0, 1000.0);
	Ray.Direction = FVector(0.0, 0.0, -1.0);

	{
		double AlongRay = 0.0;
		const double Dist = GizmoMath::DistanceRayToSegment(Ray, FVector::ZeroVector, FVector::XAxisVector, 200.0, AlongRay);
		AddInfo(FString::Printf(TEXT("Hit case: distance=%.4f alongRay=%.4f"), Dist, AlongRay));
		TestEqual(TEXT("Ray straight through the segment's own start is a perfect hit"), Dist, 0.0, 0.001);
		TestEqual(TEXT("Travels exactly 1000 units to reach it"), AlongRay, 1000.0, 0.001);
	}
	{
		double AlongRay = 0.0;
		const double Dist = GizmoMath::DistanceRayToSegment(Ray, FVector(0.0, 50.0, 0.0), FVector::XAxisVector, 200.0, AlongRay);
		AddInfo(FString::Printf(TEXT("Miss case: distance=%.4f alongRay=%.4f"), Dist, AlongRay));
		TestEqual(TEXT("A segment offset 50 units sideways misses by exactly 50"), Dist, 50.0, 0.001);
	}
	return true;
}

// Verifies DistanceRayToPoint against a hand-computed 3-4-5-triangle case (scaled by 10, so a
// 30/40 offset from the ray's own line measures exactly 50).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoDistanceRayToPointTest, "GRT.Gizmo.DistanceRayToPoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoDistanceRayToPointTest::RunTest(const FString& Parameters)
{
	FGizmoInteractionRay Ray;
	Ray.Origin = FVector(0.0, 0.0, 1000.0);
	Ray.Direction = FVector(0.0, 0.0, -1.0);

	double AlongRay = 0.0;
	const double Dist = GizmoMath::DistanceRayToPoint(Ray, FVector(30.0, 40.0, 0.0), AlongRay);
	AddInfo(FString::Printf(TEXT("distance=%.4f alongRay=%.4f"), Dist, AlongRay));
	TestEqual(TEXT("3-4-5 triangle scaled by 10 measures exactly 50"), Dist, 50.0, 0.001);
	TestEqual(TEXT("Travels exactly 1000 units to reach the point's own Z"), AlongRay, 1000.0, 0.001);
	return true;
}

// Verifies DistanceRayToRing: a ray straight through the ring's own centre is 100 units short of
// the ring itself (the ring has a hole in the middle -- a centre click is a clean miss by exactly
// the ring's radius); a ray straight through a point already ON the ring is a perfect hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoDistanceRayToRingTest, "GRT.Gizmo.DistanceRayToRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoDistanceRayToRingTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	const FVector Axis = FVector::ZAxisVector;
	const double Radius = 100.0;

	{
		FGizmoInteractionRay Ray;
		Ray.Origin = FVector(0.0, 0.0, 1000.0);
		Ray.Direction = FVector(0.0, 0.0, -1.0);
		double AlongRay = 0.0;
		const double Dist = GizmoMath::DistanceRayToRing(Ray, Origin, Axis, Radius, AlongRay);
		AddInfo(FString::Printf(TEXT("Centre click: distance=%.4f alongRay=%.4f"), Dist, AlongRay));
		TestEqual(TEXT("A click dead centre misses the ring by exactly its own radius"), Dist, Radius, 0.001);
	}
	{
		FGizmoInteractionRay Ray;
		Ray.Origin = FVector(100.0, 0.0, 1000.0);
		Ray.Direction = FVector(0.0, 0.0, -1.0);
		double AlongRay = 0.0;
		const double Dist = GizmoMath::DistanceRayToRing(Ray, Origin, Axis, Radius, AlongRay);
		AddInfo(FString::Printf(TEXT("On-ring click: distance=%.4f alongRay=%.4f"), Dist, AlongRay));
		TestEqual(TEXT("A click exactly on the ring's own radius is a perfect hit"), Dist, 0.0, 0.001);
	}
	return true;
}

// Verifies DistanceRayToQuad: a click on the quad's own boundary corner is a perfect (0-distance)
// hit; a click dead centre is also a perfect hit; a click 30 units past one edge measures exactly
// 30.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoDistanceRayToQuadTest, "GRT.Gizmo.DistanceRayToQuad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoDistanceRayToQuadTest::RunTest(const FString& Parameters)
{
	const FVector Center(50.0, 50.0, 0.0);
	const FVector AxisU = FVector::XAxisVector;
	const FVector AxisV = FVector::YAxisVector;
	const double HalfSize = 20.0; // quad spans X:[30,70], Y:[30,70]

	auto ClickAt = [&](double X, double Y) -> double
	{
		FGizmoInteractionRay Ray;
		Ray.Origin = FVector(X, Y, 1000.0);
		Ray.Direction = FVector(0.0, 0.0, -1.0);
		double AlongRay = 0.0;
		return GizmoMath::DistanceRayToQuad(Ray, Center, AxisU, AxisV, HalfSize, AlongRay);
	};

	TestEqual(TEXT("A click on the quad's own corner boundary is a perfect hit"), ClickAt(30.0, 30.0), 0.0, 0.001);
	TestEqual(TEXT("A click dead centre is a perfect hit"), ClickAt(50.0, 50.0), 0.0, 0.001);
	TestEqual(TEXT("A click 30 units past the quad's edge measures exactly 30"), ClickAt(100.0, 50.0), 30.0, 0.001);
	return true;
}
