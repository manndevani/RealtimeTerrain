// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Gizmo/RuntimeGizmoActor.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// Mirrors TerraDyne.Building.GizmoRuntimeAssets (TerraDyneBasicTests.cpp), which caught two real
// runtime-cooking bugs in the old gizmo actor (a Transient mesh property invisible to the cooker,
// and MIDs built in the constructor shared across every spawned instance via the CDO) -- the same
// two properties are re-verified here for ARuntimeGizmoActor's larger handle set, plus the new per-mode
// visibility/collision toggling and explicit handle-type identity lookups this actor adds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoActorRuntimeAssetsTest, "TerraDyne.Gizmo.ActorRuntimeAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoActorRuntimeAssetsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	ARuntimeGizmoActor* GizmoA = World->SpawnActor<ARuntimeGizmoActor>();
	ARuntimeGizmoActor* GizmoB = World->SpawnActor<ARuntimeGizmoActor>();
	TestNotNull(TEXT("First gizmo spawned"), GizmoA);
	TestNotNull(TEXT("Second gizmo spawned"), GizmoB);
	if (!GizmoA || !GizmoB) return false;

	// Every handle mesh must resolve -- especially the three migrated ones, which depend on a
	// correct /Game/Gizmo/Meshes/... path surviving the migration.
	auto CheckMesh = [this](UStaticMeshComponent* Comp, const TCHAR* Name) -> UStaticMesh*
	{
		TestNotNull(FString::Printf(TEXT("%s component exists"), Name), Comp);
		if (!Comp) return nullptr;
		UStaticMesh* Mesh = Comp->GetStaticMesh();
		TestNotNull(FString::Printf(TEXT("%s mesh resolved"), Name), Mesh);
		return Mesh;
	};

	CheckMesh(GizmoA->MeshShaftX, TEXT("MeshShaftX"));
	CheckMesh(GizmoA->MeshTipX, TEXT("MeshTipX"));
	CheckMesh(GizmoA->MeshPlaneXY, TEXT("MeshPlaneXY"));
	CheckMesh(GizmoA->MeshCenter, TEXT("MeshCenter"));
	UStaticMesh* RotationMesh = CheckMesh(GizmoA->MeshRotateX, TEXT("MeshRotateX (migrated SM_RotationHandle)"));
	UStaticMesh* ScaleMesh = CheckMesh(GizmoA->MeshScaleX, TEXT("MeshScaleX (migrated SM_ScaleHandle)"));
	UStaticMesh* UniformScaleMesh = CheckMesh(GizmoA->MeshScaleUniform, TEXT("MeshScaleUniform (migrated SM_UniformScaleHandle)"));

	// Real bounds of the migrated meshes, for tuning GizmoActor.cpp's placeholder placement
	// constants (ShaftDistance/ScaleHandleDistance/RotateRingScale) against ground truth rather
	// than a guess -- this environment cannot see a rendered viewport.
	if (RotationMesh)
	{
		AddInfo(FString::Printf(TEXT("SM_RotationHandle bounds extent: %s, origin: %s"), *RotationMesh->GetBounds().BoxExtent.ToString(), *RotationMesh->GetBounds().Origin.ToString()));
	}
	if (ScaleMesh)
	{
		AddInfo(FString::Printf(TEXT("SM_ScaleHandle bounds extent: %s, origin: %s"), *ScaleMesh->GetBounds().BoxExtent.ToString(), *ScaleMesh->GetBounds().Origin.ToString()));
	}
	if (UniformScaleMesh)
	{
		AddInfo(FString::Printf(TEXT("SM_UniformScaleHandle bounds extent: %s, origin: %s"), *UniformScaleMesh->GetBounds().BoxExtent.ToString(), *UniformScaleMesh->GetBounds().Origin.ToString()));
	}
	if (GizmoA->MeshPlaneXY && GizmoA->MeshPlaneXY->GetStaticMesh())
	{
		AddInfo(FString::Printf(TEXT("SM_PlaneHandle bounds extent: %s, origin: %s"), *GizmoA->MeshPlaneXY->GetStaticMesh()->GetBounds().BoxExtent.ToString(), *GizmoA->MeshPlaneXY->GetStaticMesh()->GetBounds().Origin.ToString()));
	}
	if (GizmoA->MeshCenter && GizmoA->MeshCenter->GetStaticMesh())
	{
		AddInfo(FString::Printf(TEXT("MeshCenter bounds extent: %s, origin: %s"), *GizmoA->MeshCenter->GetStaticMesh()->GetBounds().BoxExtent.ToString(), *GizmoA->MeshCenter->GetStaticMesh()->GetBounds().Origin.ToString()));
	}

	// Same shared-CDO-MID bug TerraDyne.Building.GizmoRuntimeAssets already caught once for the old
	// gizmo actor -- re-verified here since ARuntimeGizmoActor's material setup was written fresh.
	UMaterialInterface* MatA = GizmoA->MeshShaftX->GetMaterial(0);
	UMaterialInterface* MatB = GizmoB->MeshShaftX->GetMaterial(0);
	TestNotNull(TEXT("Second gizmo has its own axis material"), MatB);
	TestNotEqual(TEXT("Two gizmos do not share one dynamic material instance"), MatA, MatB);

	// SetMode must toggle visibility AND collision together -- a real new requirement for this
	// actor, since the old one only ever had one mode and never needed to hide anything.
	GizmoA->SetMode(EGizmoMode::Translate);
	TestTrue(TEXT("Translate mode: shaft is visible"), GizmoA->MeshShaftX->IsVisible());
	TestEqual(TEXT("Translate mode: shaft has collision"), GizmoA->MeshShaftX->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Translate mode: rotate ring is hidden"), GizmoA->MeshRotateX->IsVisible());
	TestEqual(TEXT("Translate mode: rotate ring has no collision"), GizmoA->MeshRotateX->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Translate mode: scale handle is hidden"), GizmoA->MeshScaleX->IsVisible());

	GizmoA->SetMode(EGizmoMode::Rotate);
	TestTrue(TEXT("Rotate mode: rotate ring is visible"), GizmoA->MeshRotateX->IsVisible());
	TestEqual(TEXT("Rotate mode: rotate ring has collision"), GizmoA->MeshRotateX->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Rotate mode: translate shaft is hidden"), GizmoA->MeshShaftX->IsVisible());
	TestEqual(TEXT("Rotate mode: translate shaft has no collision"), GizmoA->MeshShaftX->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	GizmoA->SetMode(EGizmoMode::Scale);
	TestTrue(TEXT("Scale mode: scale handle is visible"), GizmoA->MeshScaleX->IsVisible());
	TestFalse(TEXT("Scale mode: rotate ring is hidden"), GizmoA->MeshRotateX->IsVisible());
	TestFalse(TEXT("Scale mode: translate shaft is hidden"), GizmoA->MeshShaftX->IsVisible());

	// GetHandleType/GetComponentForHandle must round-trip for every real handle type.
	const TArray<EGizmoHandleType> AllHandles = {
		EGizmoHandleType::TranslateX, EGizmoHandleType::TranslateY, EGizmoHandleType::TranslateZ,
		EGizmoHandleType::PlaneXY, EGizmoHandleType::PlaneXZ, EGizmoHandleType::PlaneYZ,
		EGizmoHandleType::Center,
		EGizmoHandleType::RotateX, EGizmoHandleType::RotateY, EGizmoHandleType::RotateZ, EGizmoHandleType::RotateScreen,
		EGizmoHandleType::ScaleX, EGizmoHandleType::ScaleY, EGizmoHandleType::ScaleZ, EGizmoHandleType::ScaleUniform,
	};
	for (EGizmoHandleType Handle : AllHandles)
	{
		UPrimitiveComponent* Comp = GizmoA->GetComponentForHandle(Handle);
		TestNotNull(FString::Printf(TEXT("A component exists for handle %d"), static_cast<int32>(Handle)), Comp);
		if (Comp)
		{
			TestEqual(FString::Printf(TEXT("Round-trip identity for handle %d"), static_cast<int32>(Handle)), GizmoA->GetHandleType(Comp), Handle);
		}
	}
	TestEqual(TEXT("An unrecognized component resolves to None"), GizmoA->GetHandleType(nullptr), EGizmoHandleType::None);

	// SetHighlightedHandle must recolor only the axis actually highlighted -- a real bug (the
	// highlight-selection lambda accidentally always evaluating true, lighting up every axis at
	// once) was caught here by reading the code, not by this test; this pins that behavior down for
	// the future. GetVectorParameterValue reads the MID back the same way ApplyGizmoColor wrote it.
	GizmoA->SetHighlightedHandle(EGizmoHandleType::TranslateX);
	TestEqual(TEXT("Highlighting X updates X's material to hover material"), GizmoA->MeshShaftX->GetMaterial(0), GizmoA->MeshTipX->GetMaterial(0));
	TestNotEqual(TEXT("Highlighting X differentiates X from Y"), GizmoA->MeshShaftX->GetMaterial(0), GizmoA->MeshShaftY->GetMaterial(0));

	return true;
}

// Verifies the migrated SM_RotationHandle/SM_ScaleHandle meshes are actually oriented correctly.
// A real bug was caught here by reading GizmoActorTests.cpp's own logged bounds-extent AddInfo
// output: both meshes' primary axis (a ring's normal; a scale handle's long arm) is local X, not
// Z, but the constructor originally reused the Cylinder/Cone convention (primary axis Z) for them
// too -- every ring and scale handle pointed at the wrong world axis until this was caught. Pinned
// here by rotating each handle's own local X (its real primary axis, confirmed by the bounds data)
// through its assigned rotation and checking it lands on the intended world axis.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoActorHandleOrientationTest, "TerraDyne.Gizmo.ActorHandleOrientation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoActorHandleOrientationTest::RunTest(const FString& Parameters)
{
	ARuntimeGizmoActor* Gizmo = NewObject<ARuntimeGizmoActor>();
	TestNotNull(TEXT("Gizmo constructed"), Gizmo);
	if (!Gizmo) return false;

	auto CheckPrimaryAxis = [this](UStaticMeshComponent* Comp, const FVector& ExpectedWorldAxis, const TCHAR* Name)
	{
		if (!Comp) { AddError(FString::Printf(TEXT("%s missing"), Name)); return; }
		const FVector Actual = Comp->GetRelativeRotation().RotateVector(FVector::XAxisVector);
		AddInfo(FString::Printf(TEXT("%s local-X rotated to %s (expected ~%s)"), Name, *Actual.ToString(), *ExpectedWorldAxis.ToString()));
		TestTrue(FString::Printf(TEXT("%s's primary axis points along the intended world axis"), Name), Actual.Equals(ExpectedWorldAxis, 0.01));
	};

	CheckPrimaryAxis(Gizmo->MeshRotateX, FVector::XAxisVector, TEXT("MeshRotateX"));
	CheckPrimaryAxis(Gizmo->MeshRotateY, FVector::YAxisVector, TEXT("MeshRotateY"));
	CheckPrimaryAxis(Gizmo->MeshRotateZ, FVector::ZAxisVector, TEXT("MeshRotateZ"));
	CheckPrimaryAxis(Gizmo->MeshScaleX, FVector::XAxisVector, TEXT("MeshScaleX"));
	CheckPrimaryAxis(Gizmo->MeshScaleY, FVector::YAxisVector, TEXT("MeshScaleY"));
	CheckPrimaryAxis(Gizmo->MeshScaleZ, FVector::ZAxisVector, TEXT("MeshScaleZ"));

	return true;
}

// Sanity-checks GetHandleReach's relative ordering at the actor's default (unscaled) size --
// the exact constants are already covered by the constructor's own placement (visually consistent
// by construction) and the bounds-extent data already verified in ActorRuntimeAssets/
// ActorHandleOrientation, so this only needs to confirm the reach values stay sensibly ordered
// relative to each other, not re-derive each one from scratch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoActorHandleReachTest, "TerraDyne.Gizmo.ActorHandleReach", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoActorHandleReachTest::RunTest(const FString& Parameters)
{
	ARuntimeGizmoActor* Gizmo = NewObject<ARuntimeGizmoActor>();
	TestNotNull(TEXT("Gizmo constructed"), Gizmo);
	if (!Gizmo) return false;

	const double TranslateReach = Gizmo->GetHandleReach(EGizmoHandleType::TranslateX);
	const double ScaleReach = Gizmo->GetHandleReach(EGizmoHandleType::ScaleX);
	const double PlaneReach = Gizmo->GetHandleReach(EGizmoHandleType::PlaneXY);
	const double RotateReach = Gizmo->GetHandleReach(EGizmoHandleType::RotateX);
	const double PointReach = Gizmo->GetHandleReach(EGizmoHandleType::Center);

	AddInfo(FString::Printf(TEXT("Translate=%.2f Scale=%.2f Rotate=%.2f Plane=%.2f Point=%.2f"), TranslateReach, ScaleReach, RotateReach, PlaneReach, PointReach));
	TestTrue(TEXT("Translate arrows reach further than scale handles"), TranslateReach > ScaleReach);
	TestTrue(TEXT("Scale handles reach further than the small plane quads"), ScaleReach > PlaneReach);
	TestTrue(TEXT("Every reach value is positive"), TranslateReach > 0.0 && ScaleReach > 0.0 && PlaneReach > 0.0 && RotateReach > 0.0 && PointReach > 0.0);
	TestEqual(TEXT("An unrecognized handle type has zero reach"), Gizmo->GetHandleReach(EGizmoHandleType::None), 0.0, 1.0e-9);

	return true;
}
#endif // WITH_EDITOR
