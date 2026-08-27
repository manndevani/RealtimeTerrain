// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// End-to-end: driving a real ATerraDyneBuildingActor through UGizmoManipulationSubsystem's Rotate
// and Scale modes -- via the exact same SetTarget/BeginManipulation/UpdateManipulation/
// EndManipulation calls ATerraDyneEditController makes, with hand-verified rays landing on RotateZ
// and ScaleUniform (same geometry TerraDyne.Gizmo.DistanceRayToRing/DistanceRayToPoint already
// pin down, and the same ray-construction style TerraDyne.Gizmo.SubsystemBeginUpdateEnd already
// uses for TranslateX) -- actually rotates/scales the building's real footprint, not just its bare
// actor transform. Phase 4/5 already prove the generic subsystem math is mode-agnostic; this is the
// building-specific piece: does GetFootprintPolygonWorld (what terrain pad leveling, wall
// generation, and everything else downstream actually reads) respond correctly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneBuildingGizmoRotateScaleTest, "TerraDyne.Building.GizmoRotateAndScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTerraDyneBuildingGizmoRotateScaleTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	UGizmoManipulationSubsystem* Subsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
	TestNotNull(TEXT("Subsystem exists"), Subsystem);
	if (!Subsystem) return false;

	const FVector TestLocation(1000.0, -500.0, 200.0);
	ATerraDyneBuildingActor* Building = World->SpawnActor<ATerraDyneBuildingActor>(
		ATerraDyneBuildingActor::StaticClass(), FTransform(TestLocation));
	TestNotNull(TEXT("Building spawned"), Building);
	if (!Building) return false;

	Subsystem->SetTarget(Building->GetRootComponent());
	TestEqual(TEXT("SetTarget records the building's root"), Subsystem->GetTarget(), Building->GetRootComponent());

	const FVector CameraForward(0.0, 0.0, -1.0);

	// --- Rotate: grab RotateZ (ring radius 138 = NativeRingRadius 92 * RotateRingScale 1.5, at the
	// gizmo's default actor scale of 1) and turn a quarter turn. ---
	Subsystem->SetMode(EGizmoMode::Rotate);

	TArray<FVector2D> FootprintBeforeRotate = Building->GetFootprintPolygonWorld();
	TestTrue(TEXT("Footprint has points before rotating"), FootprintBeforeRotate.Num() > 0);

	FGizmoInteractionRay RotateGrabRay;
	RotateGrabRay.Origin = TestLocation + FVector(138.0, 0.0, 1000.0);
	RotateGrabRay.Direction = FVector(0.0, 0.0, -1.0);
	const EGizmoHandleType RotateGrabbed = Subsystem->BeginManipulation(RotateGrabRay, CameraForward);
	AddInfo(FString::Printf(TEXT("Rotate grab -- handle: %d"), static_cast<int32>(RotateGrabbed)));
	TestEqual(TEXT("The ray grabs RotateZ"), RotateGrabbed, EGizmoHandleType::RotateZ);

	// A quarter turn around Z: the drag ray now lands 138 units along +Y instead of +X.
	FGizmoInteractionRay RotateDragRay;
	RotateDragRay.Origin = TestLocation + FVector(0.0, 138.0, 1000.0);
	RotateDragRay.Direction = FVector(0.0, 0.0, -1.0);
	Subsystem->UpdateManipulation(RotateDragRay, CameraForward);

	TestFalse(TEXT("The building's yaw actually changed"), FMath::IsNearlyZero(Building->GetActorRotation().Yaw, 1.0));

	const TArray<FVector2D> FootprintAfterRotate = Building->GetFootprintPolygonWorld();
	TestEqual(TEXT("Same number of footprint points after rotating"), FootprintAfterRotate.Num(), FootprintBeforeRotate.Num());
	bool bDistancesPreserved = FootprintAfterRotate.Num() == FootprintBeforeRotate.Num();
	for (int32 i = 0; bDistancesPreserved && i < FootprintAfterRotate.Num(); ++i)
	{
		// A rigid rotation about the pivot (the building's own origin, TestLocation) preserves every
		// point's distance from that pivot -- unlike a translation or a scale, which wouldn't.
		const double DistBefore = FVector2D::Distance(FootprintBeforeRotate[i], FVector2D(TestLocation.X, TestLocation.Y));
		const double DistAfter = FVector2D::Distance(FootprintAfterRotate[i], FVector2D(TestLocation.X, TestLocation.Y));
		if (FMath::Abs(DistBefore - DistAfter) > 1.0)
		{
			bDistancesPreserved = false;
			AddError(FString::Printf(TEXT("Footprint point %d distance-from-pivot changed under rotation: %.2f -> %.2f"), i, DistBefore, DistAfter));
		}
	}
	TestTrue(TEXT("Rotation preserves every footprint point's distance from the pivot"), bDistancesPreserved);

	Subsystem->EndManipulation();
	TestFalse(TEXT("Not manipulating after ending the rotate drag"), Subsystem->IsManipulating());

	// --- Scale: grab ScaleZ. Its hit-test segment runs from the gizmo origin out to Reach (150cm,
	// ScaleHandleDistance) along world Z -- approaching along a HORIZONTAL ray (perpendicular to Z,
	// not the vertical rays used everywhere else in this file) at exactly that height (Z=150)
	// lands the hit cleanly on the segment's far end, safely away from the origin-adjacent zone
	// where it would otherwise tie with ScaleUniform's own point test (both ScaleZ's segment-start
	// and ScaleUniform's point sit at the exact same location -- a vertical ray through the origin,
	// the pattern this file uses everywhere else, cannot tell the two apart). ---
	Subsystem->SetMode(EGizmoMode::Scale);

	const FVector HorizontalCameraForward(-1.0, 0.0, 0.0);

	FGizmoInteractionRay ScaleGrabRay;
	ScaleGrabRay.Origin = TestLocation + FVector(1000.0, 0.0, 150.0);
	ScaleGrabRay.Direction = FVector(-1.0, 0.0, 0.0);
	const EGizmoHandleType ScaleGrabbed = Subsystem->BeginManipulation(ScaleGrabRay, HorizontalCameraForward);
	AddInfo(FString::Printf(TEXT("Scale grab -- handle: %d"), static_cast<int32>(ScaleGrabbed)));
	TestEqual(TEXT("The ray grabs ScaleZ"), ScaleGrabbed, EGizmoHandleType::ScaleZ);

	const FVector ScaleBeforeDrag = Building->GetActorScale3D();

	// ResolveAxisDragPlane picks the plane through the origin normal to the ray's own component
	// perpendicular to the Z axis -- for this horizontal ray, that's exactly the plane X = TestLocation.X.
	// Dragging to Z=300 (double the grab's Z=150) intersects it at TestLocation + (0,0,300), giving
	// ComputeScaleDelta's axis branch a ratio of exactly 300/150 = 2.0.
	FGizmoInteractionRay ScaleDragRay;
	ScaleDragRay.Origin = TestLocation + FVector(1000.0, 0.0, 300.0);
	ScaleDragRay.Direction = FVector(-1.0, 0.0, 0.0);
	Subsystem->UpdateManipulation(ScaleDragRay, HorizontalCameraForward);

	const FVector ScaleAfterDrag = Building->GetActorScale3D();
	AddInfo(FString::Printf(TEXT("Scale before: %s, after: %s"), *ScaleBeforeDrag.ToString(), *ScaleAfterDrag.ToString()));
	TestTrue(TEXT("Z scale exactly doubled"), FMath::IsNearlyEqual(ScaleAfterDrag.Z, ScaleBeforeDrag.Z * 2.0, 0.05));
	TestTrue(TEXT("X scale untouched by a Z-axis-only handle"), FMath::IsNearlyEqual(ScaleAfterDrag.X, ScaleBeforeDrag.X, 0.01));
	TestTrue(TEXT("Y scale untouched by a Z-axis-only handle"), FMath::IsNearlyEqual(ScaleAfterDrag.Y, ScaleBeforeDrag.Y, 0.01));

	Subsystem->EndManipulation();
	// EndManipulation only fires a delegate -- committing (SnapToPadHeight/NotifyBuildingChanged) is
	// the calling controller's job (see ATerraDyneEditController::OnLeftClickStop's gizmo branch).
	// Simulated here so the replication-sync assertion below exercises the real code path.
	Building->NotifyBuildingChanged();

	TestTrue(TEXT("ReplicatedScale3D was resynced to the real post-drag scale"), Building->ReplicatedScale3D.Equals(ScaleAfterDrag, 0.01));

	return true;
}
#endif // WITH_EDITOR
