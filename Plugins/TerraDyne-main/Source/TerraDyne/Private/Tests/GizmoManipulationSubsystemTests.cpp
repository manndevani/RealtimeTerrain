// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Gizmo/RuntimeGizmoActor.h"
#include "Engine/StaticMeshActor.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// End-to-end: SetTarget spawns/shows a gizmo actor at the target's location; a ray aimed at the
// TranslateX arrow (built with the exact geometry this session already hand-verified in
// TerraDyne.Gizmo.DistanceRayToSegment, offset to the target's own world location) is recognized as
// a TranslateX hit; dragging the cursor further along X moves the target by exactly that amount;
// EndManipulation commits the result and fires OnManipulationEnd (not Cancel).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoSubsystemBeginUpdateEndTest, "TerraDyne.Gizmo.SubsystemBeginUpdateEnd", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoSubsystemBeginUpdateEndTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	UGizmoManipulationSubsystem* Subsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
	TestNotNull(TEXT("Subsystem exists"), Subsystem);
	if (!Subsystem) return false;

	const FVector TestLocation(1000.0, 2000.0, 300.0);
	AStaticMeshActor* TargetActor = World->SpawnActor<AStaticMeshActor>(TestLocation, FRotator::ZeroRotator);
	TestNotNull(TEXT("Target actor spawned"), TargetActor);
	if (!TargetActor) return false;

	Subsystem->SetTarget(TargetActor->GetRootComponent());
	TestEqual(TEXT("SetTarget records the target"), Subsystem->GetTarget(), TargetActor->GetRootComponent());
	TestFalse(TEXT("Not manipulating yet"), Subsystem->IsManipulating());

	bool bBeginFired = false;
	bool bEndFired = false;
	bool bCancelFired = false;
	Subsystem->OnManipulationBegin.AddLambda([&bBeginFired](USceneComponent*) { bBeginFired = true; });
	Subsystem->OnManipulationEnd.AddLambda([&bEndFired](USceneComponent*) { bEndFired = true; });
	Subsystem->OnManipulationCancel.AddLambda([&bCancelFired](USceneComponent*) { bCancelFired = true; });

	// A ray straight down through a point exactly 100 units along the gizmo's TranslateX arrow --
	// the same hand-verified geometry as TerraDyne.Gizmo.DistanceRayToSegment's hit case, offset to
	// this test's own TestLocation.
	FGizmoInteractionRay GrabRay;
	GrabRay.Origin = TestLocation + FVector(100.0, 0.0, 1000.0);
	GrabRay.Direction = FVector(0.0, 0.0, -1.0);
	const FVector CameraForward(0.0, 0.0, -1.0);

	const EGizmoHandleType Grabbed = Subsystem->BeginManipulation(GrabRay, CameraForward);
	AddInfo(FString::Printf(TEXT("Grabbed handle: %d"), static_cast<int32>(Grabbed)));
	TestEqual(TEXT("The ray grabs TranslateX"), Grabbed, EGizmoHandleType::TranslateX);
	TestTrue(TEXT("Now manipulating"), Subsystem->IsManipulating());
	TestTrue(TEXT("OnManipulationBegin fired"), bBeginFired);

	// Move the cursor 50 units further along X.
	FGizmoInteractionRay DragRay = GrabRay;
	DragRay.Origin.X += 50.0;
	Subsystem->UpdateManipulation(DragRay, CameraForward);

	const FVector NewLocation = TargetActor->GetActorLocation();
	AddInfo(FString::Printf(TEXT("Location after drag: %s (expected ~%s)"), *NewLocation.ToString(), *(TestLocation + FVector(50.0, 0.0, 0.0)).ToString()));
	TestTrue(TEXT("The target moved ~50 units along X and nowhere else"), NewLocation.Equals(TestLocation + FVector(50.0, 0.0, 0.0), 0.5));

	Subsystem->EndManipulation();
	TestFalse(TEXT("No longer manipulating after End"), Subsystem->IsManipulating());
	TestTrue(TEXT("OnManipulationEnd fired"), bEndFired);
	TestFalse(TEXT("OnManipulationCancel did not fire"), bCancelFired);
	TestTrue(TEXT("The dragged position is kept after End (not reverted)"), TargetActor->GetActorLocation().Equals(TestLocation + FVector(50.0, 0.0, 0.0), 0.5));

	return true;
}

// The same grab-and-drag as above, but Cancel instead of End -- the target must end up back at
// EXACTLY its transform from before BeginManipulation, and OnManipulationCancel (not End) fires.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoSubsystemCancelRestoresExactlyTest, "TerraDyne.Gizmo.SubsystemCancelRestoresExactly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoSubsystemCancelRestoresExactlyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	UGizmoManipulationSubsystem* Subsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
	TestNotNull(TEXT("Subsystem exists"), Subsystem);
	if (!Subsystem) return false;

	const FVector TestLocation(-500.0, 750.0, 100.0);
	AStaticMeshActor* TargetActor = World->SpawnActor<AStaticMeshActor>(TestLocation, FRotator::ZeroRotator);
	TestNotNull(TEXT("Target actor spawned"), TargetActor);
	if (!TargetActor) return false;

	const FTransform OriginalTransform = TargetActor->GetActorTransform();

	Subsystem->SetTarget(TargetActor->GetRootComponent());

	bool bEndFired = false;
	bool bCancelFired = false;
	Subsystem->OnManipulationEnd.AddLambda([&bEndFired](USceneComponent*) { bEndFired = true; });
	Subsystem->OnManipulationCancel.AddLambda([&bCancelFired](USceneComponent*) { bCancelFired = true; });

	FGizmoInteractionRay GrabRay;
	GrabRay.Origin = TestLocation + FVector(100.0, 0.0, 1000.0);
	GrabRay.Direction = FVector(0.0, 0.0, -1.0);
	const FVector CameraForward(0.0, 0.0, -1.0);

	const EGizmoHandleType Grabbed = Subsystem->BeginManipulation(GrabRay, CameraForward);
	TestEqual(TEXT("The ray grabs TranslateX"), Grabbed, EGizmoHandleType::TranslateX);

	FGizmoInteractionRay DragRay = GrabRay;
	DragRay.Origin.X += 300.0;
	Subsystem->UpdateManipulation(DragRay, CameraForward);
	TestFalse(TEXT("The target actually moved before cancelling"), TargetActor->GetActorLocation().Equals(TestLocation, 0.5));

	Subsystem->CancelManipulation();
	TestFalse(TEXT("No longer manipulating after Cancel"), Subsystem->IsManipulating());
	TestTrue(TEXT("OnManipulationCancel fired"), bCancelFired);
	TestFalse(TEXT("OnManipulationEnd did not fire"), bEndFired);

	const FTransform RestoredTransform = TargetActor->GetActorTransform();
	AddInfo(FString::Printf(TEXT("Restored location: %s (original: %s)"), *RestoredTransform.GetLocation().ToString(), *OriginalTransform.GetLocation().ToString()));
	TestTrue(TEXT("Cancel restores the transform exactly, not approximately"), RestoredTransform.GetLocation().Equals(OriginalTransform.GetLocation(), 0.01));
	TestTrue(TEXT("Rotation is also restored exactly"), RestoredTransform.GetRotation().Equals(OriginalTransform.GetRotation(), 0.001));

	return true;
}

// UpdateManipulation's bInvertSnapForThisCall param (the Ctrl-held-during-drag convention) XORs
// against the persistent snapping setting, live, with no state left over once it stops being passed
// true -- a single ongoing drag (100-unit snap increment, 137-unit raw delta) is walked through all
// three combinations without ever calling BeginManipulation/EndManipulation again in between.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoSubsystemInvertSnapTest, "TerraDyne.Gizmo.SubsystemInvertSnap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGizmoSubsystemInvertSnapTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	UGizmoManipulationSubsystem* Subsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
	TestNotNull(TEXT("Subsystem exists"), Subsystem);
	if (!Subsystem) return false;

	const FVector TestLocation(200.0, -400.0, 50.0);
	AStaticMeshActor* TargetActor = World->SpawnActor<AStaticMeshActor>(TestLocation, FRotator::ZeroRotator);
	TestNotNull(TEXT("Target actor spawned"), TargetActor);
	if (!TargetActor) return false;

	Subsystem->SetTarget(TargetActor->GetRootComponent());
	Subsystem->SetTranslationSnap(100.0f);
	Subsystem->SetSnappingEnabled(false);

	FGizmoInteractionRay GrabRay;
	GrabRay.Origin = TestLocation + FVector(100.0, 0.0, 1000.0);
	GrabRay.Direction = FVector(0.0, 0.0, -1.0);
	const FVector CameraForward(0.0, 0.0, -1.0);
	TestEqual(TEXT("The ray grabs TranslateX"), Subsystem->BeginManipulation(GrabRay, CameraForward), EGizmoHandleType::TranslateX);

	// Raw delta along X is 137 units for the rest of this test -- nearest-100 snap rounds that to 100.
	FGizmoInteractionRay DragRay = GrabRay;
	DragRay.Origin.X += 137.0;

	Subsystem->UpdateManipulation(DragRay, CameraForward, /*bInvertSnapForThisCall=*/true);
	FVector Loc = TargetActor->GetActorLocation();
	AddInfo(FString::Printf(TEXT("Persistent off + invert true -> %s (expect snapped +100)"), *Loc.ToString()));
	TestTrue(TEXT("Ctrl held while the persistent setting is off snaps this drag (137 -> 100)"), Loc.Equals(TestLocation + FVector(100.0, 0.0, 0.0), 0.5));

	Subsystem->SetSnappingEnabled(true);
	Subsystem->UpdateManipulation(DragRay, CameraForward, /*bInvertSnapForThisCall=*/true);
	Loc = TargetActor->GetActorLocation();
	AddInfo(FString::Printf(TEXT("Persistent on + invert true -> %s (expect raw +137)"), *Loc.ToString()));
	TestTrue(TEXT("Ctrl held while the persistent setting is on un-snaps this drag (raw 137)"), Loc.Equals(TestLocation + FVector(137.0, 0.0, 0.0), 0.5));

	Subsystem->UpdateManipulation(DragRay, CameraForward, /*bInvertSnapForThisCall=*/false);
	Loc = TargetActor->GetActorLocation();
	AddInfo(FString::Printf(TEXT("Persistent on + invert false -> %s (expect snapped +100)"), *Loc.ToString()));
	TestTrue(TEXT("Releasing Ctrl the very next call falls straight back to the persistent (on) setting -- no leftover state"), Loc.Equals(TestLocation + FVector(100.0, 0.0, 0.0), 0.5));

	Subsystem->CancelManipulation();
	return true;
}
#endif // WITH_EDITOR
