// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/GRTEditController.h"
#include "Core/GRTManager.h"
#include "Core/GRTSubsystem.h"
#include "World/GRTChunk.h"
#include "Building/GRTBuildingActor.h"
#include "Building/GRTPlacedModelActor.h"
#include "Building/GRTGizmoActor.h"
#include "Core/GRTAssetLibrarySubsystem.h"
#include "IO/GRTOSMParser.h"
#include "IO/GRTOSMIngest.h"
#include "CesiumGeoreference.h"
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Gizmo/RuntimeGizmoActor.h"
#include "Components/SplineComponent.h"
#include "GRTModule.h"
#include "Engine/World.h"
#include "UI/GRTToolWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
// Controller.h only forward-declares APlayerState; the rejection log upcasts it to UObject*.
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Camera/PlayerCameraManager.h"

AGRTEditController::AGRTEditController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;
	UIClass = UGRTToolWidget::StaticClass();
	bReplicates = true;
}

void AGRTEditController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGRTEditController, bCanEditTerrain);
}

bool AGRTEditController::IsInstructor() const
{
	// On a listen server the host is both local and authoritative; remote clients can edit if bCanEditTerrain is enabled
	return IsLocalController() && (HasAuthority() || bCanEditTerrain);
}

bool AGRTEditController::HasEditAuthority() const
{
	return IsLocalController() || bCanEditTerrain;
}

/**
 * Rejects a Server RPC from a controller with no edit rights, and logs who tried.
 *
 * Every Server_* body starts with this. Without it the RPCs were open to any connected client:
 * bCanEditTerrain gated the tool UI on the sending machine only, and a client that never opens
 * that UI can still call the RPC.
 */
#define GRT_REQUIRE_EDIT_AUTHORITY()                                                      \
	if (!HasEditAuthority())                                                                    \
	{                                                                                           \
		UE_LOG(LogGRT, Warning,                                                           \
			TEXT("Rejected %hs from '%s' — controller has no edit rights (bCanEditTerrain)."),  \
			__func__, *GetNameSafe(PlayerState));                                               \
		return;                                                                                 \
	}

AGRTManager* AGRTEditController::GetManager() const
{
	UGRTSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UGRTSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetTerrainManager() : nullptr;
}

void AGRTEditController::BeginPlay()
{
	Super::BeginPlay();

	PrimaryActorTick.bCanEverTick = true;

	// A client joining an in-progress session has correct procedural terrain (same noise, same
	// seed) but none of the edits made before it connected. Push the authoritative state for every
	// live chunk so it catches up.
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->SendFullSyncToController(this);
		}
	}

	// Tool panel is per-machine UI, so only the local player sitting at this client builds one.
	if (IsLocalController() && (IsInstructor() || bShowUIForClients) && UIClass)
	{
		ActiveUI = CreateWidget<UUserWidget>(this, UIClass);
		if (ActiveUI)
		{
			ActiveUI->AddToViewport(100);

			// Nothing has spawned yet if bShowWorldModeChoiceOnBeginPlay is on (see
			// AGRTManager::BeginPlay) -- this is what actually asks the player to pick Sandbox
			// or Import GIS File, drawn over the tool panel just added above.
			if (UGRTToolWidget* ToolUI = Cast<UGRTToolWidget>(ActiveUI))
			{
				ToolUI->ShowWorldModeChoiceIfNeeded();
			}
		}

		SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		// Without this the cursor is hidden and pinned for the duration of a press, so the brush
		// stays where the stroke began instead of following the drag.
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
}

void AGRTEditController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HolographicGhostBuilding && !HolographicGhostBuilding->IsActorBeingDestroyed())
	{
		HolographicGhostBuilding->Destroy();
		HolographicGhostBuilding = nullptr;
	}
	if (HolographicGhostModel && !HolographicGhostModel->IsActorBeingDestroyed())
	{
		HolographicGhostModel->Destroy();
		HolographicGhostModel = nullptr;
	}
	if (ActiveMoveGizmo && !ActiveMoveGizmo->IsActorBeingDestroyed())
	{
		ActiveMoveGizmo->Destroy();
		ActiveMoveGizmo = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AGRTEditController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGRTEditController::OnLeftClickStart);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AGRTEditController::OnLeftClickStop);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AGRTEditController::OnCancelManipulationPressed);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AGRTEditController::OnToggleGizmoSpacePressed);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AGRTEditController::OnToggleGizmoSnappingPressed);
	// Ctrl+Z / Ctrl+Y instead of bare keys, so Z and Y stay available for other input. Shift+W/E/R
	// instead of bare keys for the same reason -- W/E are also the (unconditional, modifier-blind)
	// axis-mapping keys for camera forward/up movement, so AGRTEditPawn separately suppresses
	// its own movement handlers while Shift is held, keeping Shift+W/E/R free of any camera nudge.
	{
		FInputKeyBinding UndoBinding(FInputChord(EKeys::Z, false, true, false, false), IE_Pressed);
		UndoBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AGRTEditController::OnUndoPressed);
		InputComponent->KeyBindings.Add(MoveTemp(UndoBinding));

		FInputKeyBinding RedoBinding(FInputChord(EKeys::Y, false, true, false, false), IE_Pressed);
		RedoBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AGRTEditController::OnRedoPressed);
		InputComponent->KeyBindings.Add(MoveTemp(RedoBinding));

		FInputKeyBinding TranslateModeBinding(FInputChord(EKeys::W, true, false, false, false), IE_Pressed);
		TranslateModeBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AGRTEditController::OnSetGizmoModeTranslate);
		InputComponent->KeyBindings.Add(MoveTemp(TranslateModeBinding));

		FInputKeyBinding RotateModeBinding(FInputChord(EKeys::E, true, false, false, false), IE_Pressed);
		RotateModeBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AGRTEditController::OnSetGizmoModeRotate);
		InputComponent->KeyBindings.Add(MoveTemp(RotateModeBinding));

		FInputKeyBinding ScaleModeBinding(FInputChord(EKeys::R, true, false, false, false), IE_Pressed);
		ScaleModeBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AGRTEditController::OnSetGizmoModeScale);
		InputComponent->KeyBindings.Add(MoveTemp(ScaleModeBinding));
	}
}

UGRTToolWidget* AGRTEditController::GetToolWidget() const
{
	return Cast<UGRTToolWidget>(ActiveUI);
}

bool AGRTEditController::TraceUnderCursor(FHitResult& OutHit) const
{
	FVector WorldLoc, WorldDir;
	if (!const_cast<AGRTEditController*>(this)->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}

	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = true;
	if (APawn* MyPawn = GetPawn())
	{
		TraceParams.AddIgnoredActor(MyPawn);
	}
	if (HolographicGhostBuilding)
	{
		TraceParams.AddIgnoredActor(HolographicGhostBuilding);
	}
	if (ActiveMoveGizmo)
	{
		TraceParams.AddIgnoredActor(ActiveMoveGizmo);
	}
	if (GizmoSubsystem)
	{
		if (AActor* RuntimeGizmo = GizmoSubsystem->GetGizmoActor())
		{
			TraceParams.AddIgnoredActor(RuntimeGizmo);
		}
	}

	// 50,000,000 uu (500 km) reaches terrain from orbital and high flight altitudes across large GIS datasets
	return GetWorld()->LineTraceSingleByChannel(
		OutHit, WorldLoc, WorldLoc + WorldDir * 50000000.0f, ECC_WorldStatic, TraceParams);
}

bool AGRTEditController::TraceSplinePointUnderCursor(int32& OutPointIndex, FVector& OutHitPoint) const
{
	OutPointIndex = INDEX_NONE;
	if (!ActiveMoveGizmo || ActiveMoveGizmo->IsHidden())
	{
		return false;
	}

	FVector WorldLoc, WorldDir;
	if (!const_cast<AGRTEditController*>(this)->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}

	const FVector TraceEnd = WorldLoc + WorldDir * 200000.0f;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(TraceSplinePoint), false);

	float BestDistance = MAX_FLT;
	int32 BestIndex = INDEX_NONE;
	FVector BestLocation = FVector::ZeroVector;

	for (int32 i = 0; i < ActiveMoveGizmo->SplinePointMeshHandles.Num(); ++i)
	{
		if (UStaticMeshComponent* HandleComp = ActiveMoveGizmo->SplinePointMeshHandles[i])
		{
			FHitResult CompHit;
			if (HandleComp->LineTraceComponent(CompHit, WorldLoc, TraceEnd, TraceParams))
			{
				if (CompHit.Distance < BestDistance)
				{
					BestDistance = CompHit.Distance;
					BestIndex = i;
					BestLocation = CompHit.Location;
				}
			}
		}
	}

	if (BestIndex != INDEX_NONE)
	{
		OutPointIndex = BestIndex;
		OutHitPoint = BestLocation;
		return true;
	}

	return false;
}

bool AGRTEditController::IntersectCursorWithPlane(const FPlane& Plane, FVector& OutIntersection) const
{
	FVector WorldLoc, WorldDir;
	if (!const_cast<AGRTEditController*>(this)->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}

	OutIntersection = FMath::RayPlaneIntersection(WorldLoc, WorldDir, Plane);
	return true;
}

bool AGRTEditController::BuildGizmoInteractionRay(FGizmoInteractionRay& OutRay) const
{
	return const_cast<AGRTEditController*>(this)->DeprojectMousePositionToWorld(OutRay.Origin, OutRay.Direction);
}

FVector AGRTEditController::GetCameraForwardVector() const
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->GetCameraRotation().Vector();
	}
	FGizmoInteractionRay Ray;
	return BuildGizmoInteractionRay(Ray) ? Ray.Direction : FVector::ForwardVector;
}

void AGRTEditController::OnLeftClickStart()
{
	bFlattenHeightLocked = false;

	UGRTToolWidget* ToolUI = GetToolWidget();
	if (ToolUI && ToolUI->SelectedBuilding.IsValid())
	{
		// 1. Check if user clicked on a Spline Vertex Handle
		int32 HitPointIndex = INDEX_NONE;
		FVector HitPointLoc;
		if (TraceSplinePointUnderCursor(HitPointIndex, HitPointLoc) && HitPointIndex != INDEX_NONE)
		{
			DraggingSplinePointIndex = HitPointIndex;
			TArray<FVector> LocalPts = ToolUI->SelectedBuilding->GatherSplinePointsLocal();
			if (LocalPts.IsValidIndex(HitPointIndex))
			{
				SplinePointDragStartLocal = LocalPts[HitPointIndex];
				const FVector WorldPt = ToolUI->SelectedBuilding->GetActorTransform().TransformPosition(SplinePointDragStartLocal);
				GizmoDragPlane = FPlane(WorldPt, FVector::UpVector);
				IntersectCursorWithPlane(GizmoDragPlane, GizmoDragStartIntersection);
				return;
			}
		}

		// 2. Check if user clicked on the shared runtime gizmo's handle
		if (GizmoSubsystem)
		{
			FGizmoInteractionRay Ray;
			if (BuildGizmoInteractionRay(Ray))
			{
				if (GizmoSubsystem->BeginManipulation(Ray, GetCameraForwardVector()) != EGizmoHandleType::None)
				{
					bIsGizmoAxisDrag = true;
					return;
				}
			}
		}
	}

	if (ToolUI && ToolUI->SelectedModelActor.IsValid())
	{
		if (GizmoSubsystem)
		{
			FGizmoInteractionRay Ray;
			if (BuildGizmoInteractionRay(Ray))
			{
				if (GizmoSubsystem->BeginManipulation(Ray, GetCameraForwardVector()) != EGizmoHandleType::None)
				{
					bIsGizmoAxisDrag = true;
					return;
				}
			}
		}
	}

	// Selecting a placed model is gated on a model tool being active. This used to run a full
	// 500 km trace on every left click in every mode, so clicking a model while sculpting silently
	// abandoned the brush stroke and selected it instead.
	if (ToolUI && IsModelToolMode(ToolUI->CurrentTool))
	{
		FHitResult ModelHit;
		if (TraceUnderCursor(ModelHit) && ModelHit.bBlockingHit)
		{
			if (AGRTPlacedModelActor* HitModel = Cast<AGRTPlacedModelActor>(ModelHit.GetActor()))
			{
				SelectModelActor(HitModel);
				return;
			}
		}
	}

	if (ToolUI && (IsBuildingToolMode(ToolUI->CurrentTool) || IsModelToolMode(ToolUI->CurrentTool)))
	{
		// Deliberately does NOT set bIsClicking: that flag is what makes Tick keep re-applying the
		// action, which is right for a brush and wrong for placing an actor.
		if (IsBuildingToolMode(ToolUI->CurrentTool))
		{
			HandleBuildingClick();
		}
		else if (IsModelToolMode(ToolUI->CurrentTool))
		{
			HandleModelClick();
		}
		return;
	}

	bIsClicking = true;
}

void AGRTEditController::HandleBuildingClick()
{
	UGRTToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI)
	{
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit) || !Hit.bBlockingHit)
	{
		return;
	}

	// Clicking an existing building always selects it, whichever building tool is active
	AActor* HitActor = Hit.GetActor();
	if (AGRTBuildingActor* HitBuilding = Cast<AGRTBuildingActor>(HitActor))
	{
		ToolUI->SelectedBuilding = HitBuilding;
		ToolUI->PendingFloorCount = HitBuilding->FloorCount;
		return;
	}

	if (ToolUI->CurrentTool != EGRTToolMode::PlaceBuilding)
	{
		ToolUI->SelectedBuilding = nullptr;
		return;
	}

	const int32 FloorCount = FMath::Max(1, ToolUI->PendingFloorCount);
	RequestPlaceBuilding(AGRTBuildingActor::StaticClass(), FTransform(Hit.Location), FloorCount);
}

void AGRTEditController::OnLeftClickStop()
{
	bIsClicking = false;
	bFlattenHeightLocked = false;

	if (DraggingSplinePointIndex != INDEX_NONE)
	{
		UGRTToolWidget* ToolUI = GetToolWidget();
		if (ToolUI && ToolUI->SelectedBuilding.IsValid())
		{
			ToolUI->SelectedBuilding->SyncSplinePointsToReplication();
			RequestSetBuildingSplinePoints(ToolUI->SelectedBuilding.Get(), ToolUI->SelectedBuilding->GatherSplinePointsLocal());
		}
		DraggingSplinePointIndex = INDEX_NONE;
		if (ActiveMoveGizmo)
		{
			ActiveMoveGizmo->SetHighlightedSplinePoint(INDEX_NONE);
		}
		return;
	}

	if (bIsGizmoAxisDrag)
	{
		bIsGizmoAxisDrag = false;
		if (GizmoSubsystem)
		{
			GizmoSubsystem->EndManipulation();
		}
		UGRTToolWidget* ToolUI = GetToolWidget();
		if (ToolUI && ToolUI->SelectedBuilding.IsValid())
		{
			RequestMoveBuilding(ToolUI->SelectedBuilding.Get(), ToolUI->SelectedBuilding->GetActorTransform());
		}
		else if (ToolUI && ToolUI->SelectedModelActor.IsValid())
		{
			RequestMoveModel(ToolUI->SelectedModelActor.Get(), ToolUI->SelectedModelActor->GetActorTransform());
		}
		return;
	}

	if (bStrokeBegun)
	{
		bStrokeBegun = false;
		RequestCommitStroke();
	}
}

void AGRTEditController::OnUndoPressed()
{
	// Priority chain: building/model/OSM actor spawns, then terrain sculpting as the fallback --
	// each tries its own history and only falls through when it had nothing to take back.
	if (!TryActorUndo())
	{
		RequestUndo();
	}
}
void AGRTEditController::OnRedoPressed()
{
	if (!TryActorRedo())
	{
		RequestRedo();
	}
}

//-----------------------------------------------------------------------------
// Building/model/OSM actor undo
//-----------------------------------------------------------------------------

void AGRTEditController::PushActorUndoEntry(FGRTActorUndoEntry&& Entry)
{
	ActorUndoStack.Add(MoveTemp(Entry));
	if (ActorUndoStack.Num() > MaxActorUndoHistory)
	{
		ActorUndoStack.RemoveAt(0);
	}
	// A fresh action invalidates whatever redo history was sitting ahead of it -- matches
	// CommitStroke's identical handling of RedoStacks for terrain sculpting.
	ActorRedoStack.Reset();
}

void AGRTEditController::DestroyActorsInEntry(FGRTActorUndoEntry& Entry)
{
	for (FGRTActorSnapshot& Snap : Entry.Actors)
	{
		if (AActor* Live = Snap.LiveActor.Get())
		{
			Live->Destroy();
		}
		Snap.LiveActor = nullptr;
	}
}

void AGRTEditController::RespawnActorsInEntry(FGRTActorUndoEntry& Entry)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (FGRTActorSnapshot& Snap : Entry.Actors)
	{
		switch (Snap.Kind)
		{
		case EGRTActorSnapshotKind::Building:
		{
			AGRTBuildingActor* Restored = World->SpawnActor<AGRTBuildingActor>(
				AGRTBuildingActor::StaticClass(),
				FTransform(Snap.BuildingData.Rotation, Snap.BuildingData.Origin), SpawnParams);
			if (Restored)
			{
				Restored->ApplyData(Snap.BuildingData, true);
			}
			Snap.LiveActor = Restored;
			break;
		}
		case EGRTActorSnapshotKind::Model:
		{
			AGRTPlacedModelActor* Restored = World->SpawnActor<AGRTPlacedModelActor>(
				AGRTPlacedModelActor::StaticClass(), Snap.ModelData.WorldTransform, SpawnParams);
			if (Restored)
			{
				Restored->ApplyData(Snap.ModelData); // ApplyData resolves geometry itself
			}
			Snap.LiveActor = Restored;
			break;
		}
		}
	}
}

bool AGRTEditController::TryActorUndo()
{
	if (!HasAuthority() || ActorUndoStack.Num() == 0)
	{
		return false;
	}

	FGRTActorUndoEntry Entry = ActorUndoStack.Pop();
	if (Entry.Op == EGRTActorUndoOp::Spawn)
	{
		DestroyActorsInEntry(Entry);
	}
	else
	{
		RespawnActorsInEntry(Entry);
	}

	ActorRedoStack.Add(MoveTemp(Entry));
	if (ActorRedoStack.Num() > MaxActorUndoHistory)
	{
		ActorRedoStack.RemoveAt(0);
	}
	return true;
}

bool AGRTEditController::TryActorRedo()
{
	if (!HasAuthority() || ActorRedoStack.Num() == 0)
	{
		return false;
	}

	FGRTActorUndoEntry Entry = ActorRedoStack.Pop();
	if (Entry.Op == EGRTActorUndoOp::Spawn)
	{
		RespawnActorsInEntry(Entry);
	}
	else
	{
		DestroyActorsInEntry(Entry);
	}

	ActorUndoStack.Add(MoveTemp(Entry));
	if (ActorUndoStack.Num() > MaxActorUndoHistory)
	{
		ActorUndoStack.RemoveAt(0);
	}
	return true;
}

void AGRTEditController::OnCancelManipulationPressed()
{
	// One call serves both consumers -- the subsystem already knows which one (if either) is
	// actually mid-drag, and CancelManipulation is a no-op if nothing is.
	if (GizmoSubsystem)
	{
		GizmoSubsystem->CancelManipulation();
	}
}

void AGRTEditController::OnSetGizmoModeTranslate()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetMode(EGizmoMode::Translate);
	}
}

void AGRTEditController::OnSetGizmoModeRotate()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetMode(EGizmoMode::Rotate);
	}
}

void AGRTEditController::OnSetGizmoModeScale()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetMode(EGizmoMode::Scale);
	}
}

void AGRTEditController::OnToggleGizmoSpacePressed()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetCoordinateSpace(
			GizmoSubsystem->GetCoordinateSpace() == EGizmoCoordinateSpace::World
				? EGizmoCoordinateSpace::Local
				: EGizmoCoordinateSpace::World);
	}
}

void AGRTEditController::OnToggleGizmoSnappingPressed()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetSnappingEnabled(!GizmoSubsystem->IsSnappingEnabled());
	}
}

void AGRTEditController::HandleGizmoManipulationCancelled(USceneComponent* CancelledTarget)
{
	// The subsystem is shared with the road tool, so a cancel could be for a road-point drag that
	// has nothing to do with us -- only react if it's our own bound building's drag being cancelled.
	if (!bIsGizmoAxisDrag || !GizmoBoundBuilding.IsValid() || CancelledTarget != GizmoBoundBuilding->GetRootComponent())
	{
		return;
	}

	bIsGizmoAxisDrag = false;

	// The subsystem already restored the building's exact pre-drag transform -- resettle the
	// pad-tracking/preview state to match, the same way a normal release would, since nothing else
	// will tell this controller the drag stopped. Deliberately does NOT call SnapToPadHeight: that
	// would recompute a new pad height and could move Z away from the transform Cancel just
	// restored, which is the opposite of what a cancel is supposed to do.
	AGRTBuildingActor* Building = GizmoBoundBuilding.Get();
	Building->PadTargetWorldZ = Building->GetActorLocation().Z;
	Building->NotifyBuildingChanged();

	if (ActiveMoveGizmo)
	{
		ActiveMoveGizmo->SetActorLocation(Building->GetActorLocation());
		ActiveMoveGizmo->UpdateSplinePointHandles(Building->GatherSplinePointsLocal(), Building->GetActorTransform());
	}
}

void AGRTEditController::PerformToolAction()
{
	float Radius = 2000.0f;
	float Strength = 1000.0f;
	EGRTToolMode Mode = EGRTToolMode::SculptRaise;
	if (UGRTToolWidget* ToolUI = GetToolWidget())
	{
		Radius = ToolUI->BrushRadius;
		Strength = ToolUI->BrushStrength * 2500.0f;
		Mode = ToolUI->CurrentTool;
	}

	// Switching to a building tool mid-drag would otherwise keep sculpting until the button comes
	// up, because bIsClicking was latched by the press that started under a sculpt tool.
	if (IsBuildingToolMode(Mode))
	{
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit))
	{
		return;
	}

	AGRTManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	if (!bStrokeBegun)
	{
		bStrokeBegun = true;
		RequestBeginStroke(Hit.Location, Radius);
	}

	EGRTBrushMode BrushMode = EGRTBrushMode::Raise;
	if (Mode == EGRTToolMode::SculptLower)
	{
		BrushMode = EGRTBrushMode::Lower;
		Strength = -FMath::Abs(Strength);
	}
	else if (Mode == EGRTToolMode::Flatten)
	{
		BrushMode = EGRTBrushMode::Flatten;
		Strength = FMath::Abs(Strength);
	}
	else if (Mode == EGRTToolMode::Smooth)
	{
		BrushMode = EGRTBrushMode::Smooth;
		Strength = FMath::Abs(Strength);
	}

	float FlattenHeight = 0.0f;
	if (BrushMode == EGRTBrushMode::Flatten)
	{
		if (!bFlattenHeightLocked)
		{
			if (AGRTChunk* HitChunk = Manager->GetChunkAtLocation(Hit.Location))
			{
				LockedFlattenHeight = HitChunk->GetHeightAtLocation(Hit.Location - HitChunk->GetActorLocation());
			}
			bFlattenHeightLocked = true;
		}
		FlattenHeight = LockedFlattenHeight;
	}

	FGRTBrushParams BrushParams;
	BrushParams.WorldLocation = Hit.Location;
	BrushParams.Radius = Radius;
	BrushParams.Strength = Strength;
	BrushParams.BrushMode = BrushMode;
	BrushParams.FlattenHeight = FlattenHeight;
	RequestBrush(BrushParams);

	DrawDebugSphere(GetWorld(), Hit.Location, Radius, 16, FColor::Green, false, -1.0f, 0, 2.0f);
}

void AGRTEditController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TimeSinceLastPadUpdate += DeltaTime;

	// Fail-safe: if the left mouse button is no longer down, terminate any active gizmo/vertex/stroke drag
	// so the actor or gizmo never gets stuck to the mouse if a release event was consumed by UI or missed.
	if ((bIsGizmoAxisDrag || DraggingSplinePointIndex != INDEX_NONE || bIsClicking) && !IsInputKeyDown(EKeys::LeftMouseButton))
	{
		OnLeftClickStop();
	}

	// 1. Handle spline point vertex drag
	if (DraggingSplinePointIndex != INDEX_NONE)
	{
		UGRTToolWidget* ToolUI = GetToolWidget();
		if (ToolUI && ToolUI->SelectedBuilding.IsValid())
		{
			FVector CurrentIntersection;
			if (IntersectCursorWithPlane(GizmoDragPlane, CurrentIntersection))
			{
				FVector LocalPoint = ToolUI->SelectedBuilding->GetActorTransform().InverseTransformPosition(CurrentIntersection);
				LocalPoint.Z = 0.0f;
				
				// Fast local mesh update without immediate terrain bake
				ToolUI->SelectedBuilding->SetSplinePointLocal(DraggingSplinePointIndex, LocalPoint, false);

				// Throttled terrain pad bake during continuous drag (~20Hz)
				if (TimeSinceLastPadUpdate >= 0.05f)
				{
					TimeSinceLastPadUpdate = 0.0f;
					ToolUI->SelectedBuilding->NotifyBuildingChanged();
				}

				if (ActiveMoveGizmo)
				{
					ActiveMoveGizmo->UpdateSplinePointHandles(ToolUI->SelectedBuilding->GatherSplinePointsLocal(), ToolUI->SelectedBuilding->GetActorTransform());
					ActiveMoveGizmo->SetHighlightedSplinePoint(DraggingSplinePointIndex);
				}
			}
		}
	}
	// 2. Handle shared runtime gizmo drag
	else if (bIsGizmoAxisDrag)
	{
		UGRTToolWidget* ToolUI = GetToolWidget();
		if (ToolUI && ToolUI->SelectedBuilding.IsValid() && GizmoSubsystem)
		{
			FGizmoInteractionRay Ray;
			if (BuildGizmoInteractionRay(Ray))
			{
				const bool bInvertSnap = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
				GizmoSubsystem->UpdateManipulation(Ray, GetCameraForwardVector(), bInvertSnap);

				const EGizmoHandleType ActiveHandle = GizmoSubsystem->GetActiveHandle();
				const bool bIsVerticalOrNonPlanar = (ActiveHandle == EGizmoHandleType::TranslateZ ||
					ActiveHandle == EGizmoHandleType::Center ||
					ActiveHandle == EGizmoHandleType::ScaleZ ||
					ActiveHandle == EGizmoHandleType::ScaleUniform ||
					ActiveHandle == EGizmoHandleType::RotateX ||
					ActiveHandle == EGizmoHandleType::RotateY);

				if (bIsVerticalOrNonPlanar)
				{
					ToolUI->SelectedBuilding->bAutoPadHeight = false;
				}

				ToolUI->SelectedBuilding->PadTargetWorldZ = ToolUI->SelectedBuilding->GetActorLocation().Z;

				// Throttled terrain pad update during continuous drag (~20Hz)
				if (TimeSinceLastPadUpdate >= 0.05f)
				{
					TimeSinceLastPadUpdate = 0.0f;

					if (!bIsVerticalOrNonPlanar && ToolUI->SelectedBuilding->bAutoPadHeight)
					{
						ToolUI->SelectedBuilding->SnapToPadHeight();
					}
					ToolUI->SelectedBuilding->NotifyBuildingChanged();
				}

				if (ActiveMoveGizmo)
				{
					ActiveMoveGizmo->SetActorLocation(ToolUI->SelectedBuilding->GetActorLocation());
					ActiveMoveGizmo->UpdateSplinePointHandles(ToolUI->SelectedBuilding->GatherSplinePointsLocal(), ToolUI->SelectedBuilding->GetActorTransform());
				}
			}
		}
		else if (ToolUI && ToolUI->SelectedModelActor.IsValid() && GizmoSubsystem)
		{
			FGizmoInteractionRay Ray;
			if (BuildGizmoInteractionRay(Ray))
			{
				const bool bInvertSnap = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
				GizmoSubsystem->UpdateManipulation(Ray, GetCameraForwardVector(), bInvertSnap);
			}
		}
	}
	else
	{
		UpdateMoveGizmo();

		if (bIsClicking && IsInstructor())
		{
			PerformToolAction();
		}
	}

	UpdateHolographicPreview();
	UpdateHolographicModelPreview();

	// Must run before the early-return below, which only guards the chunk-sync catch-up loop --
	// an OSM import queued here would otherwise only ever drain on ticks that happen to coincide
	// with a pending chunk sync.
	ProcessOSMSpawnQueue();

	if (PendingChunkSyncs.Num() == 0)
	{
		return;
	}

	AGRTManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	TArray<FIntPoint> Resolved;
	for (const TPair<FIntPoint, FGRTChunkData>& Pair : PendingChunkSyncs)
	{
		if (AGRTChunk* Chunk = Manager->GetChunkAtCoord(Pair.Key))
		{
			Chunk->LoadFromData(Pair.Value);
			Resolved.Add(Pair.Key);
		}
	}

	for (const FIntPoint& Coord : Resolved)
	{
		PendingChunkSyncs.Remove(Coord);
	}
}

void AGRTEditController::UpdateMoveGizmo()
{
	if (!GizmoSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			GizmoSubsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
			if (GizmoSubsystem)
			{
				GizmoSubsystem->OnManipulationCancel.AddUObject(this, &AGRTEditController::HandleGizmoManipulationCancelled);
			}
		}
	}

	if (!IsLocalController() || !IsInstructor())
	{
		if (ActiveMoveGizmo)
		{
			ActiveMoveGizmo->SetActorHiddenInGame(true);
		}
		if (GizmoSubsystem && GizmoBoundBuilding.IsValid())
		{
			GizmoSubsystem->ClearTarget();
			GizmoBoundBuilding = nullptr;
		}
		return;
	}

	UGRTToolWidget* ToolUI = GetToolWidget();
	AGRTBuildingActor* Selected = ToolUI ? ToolUI->SelectedBuilding.Get() : nullptr;

	if (Selected && !Selected->IsActorBeingDestroyed())
	{
		if (!ActiveMoveGizmo)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;
			ActiveMoveGizmo = GetWorld()->SpawnActor<AGRTGizmoActor>(
				AGRTGizmoActor::StaticClass(),
				FTransform(Selected->GetActorLocation()),
				SpawnParams);
		}

		if (ActiveMoveGizmo)
		{
			ActiveMoveGizmo->SetActorLocation(Selected->GetActorLocation());
			ActiveMoveGizmo->SetActorHiddenInGame(false);
			ActiveMoveGizmo->UpdateSplinePointHandles(Selected->GatherSplinePointsLocal(), Selected->GetActorTransform());
		}

		if (GizmoSubsystem)
		{
			if (GizmoBoundBuilding.Get() != Selected)
			{
				GizmoSubsystem->SetTarget(Selected->GetRootComponent());
				GizmoBoundBuilding = Selected;
			}
			else
			{
				GizmoSubsystem->UpdateGizmoTransform();
			}
		}

		if (!bIsGizmoAxisDrag && DraggingSplinePointIndex == INDEX_NONE)
		{
			float MouseX = 0.0f, MouseY = 0.0f;
			GetMousePosition(MouseX, MouseY);
			const FVector2D CurrentMousePos(MouseX, MouseY);
			if (FVector2D::DistSquared(CurrentMousePos, LastMousePosition) > 1.0f)
			{
				LastMousePosition = CurrentMousePos;
				int32 HoverPoint = INDEX_NONE;
				FVector HitPoint;
				if (TraceSplinePointUnderCursor(HoverPoint, HitPoint))
				{
					if (ActiveMoveGizmo)
					{
						ActiveMoveGizmo->SetHighlightedSplinePoint(HoverPoint);
					}
				}
				else
				{
					if (ActiveMoveGizmo)
					{
						ActiveMoveGizmo->SetHighlightedSplinePoint(INDEX_NONE);
					}
					if (GizmoSubsystem)
					{
						FGizmoInteractionRay Ray;
						if (BuildGizmoInteractionRay(Ray))
						{
							GizmoSubsystem->UpdateHover(Ray, GetCameraForwardVector());
						}
					}
				}
			}
		}
	}
	else if (AGRTPlacedModelActor* SelectedModel = ToolUI ? ToolUI->SelectedModelActor.Get() : nullptr)
	{
		if (ActiveMoveGizmo)
		{
			ActiveMoveGizmo->SetActorHiddenInGame(true);
			ActiveMoveGizmo->SetHighlightedSplinePoint(INDEX_NONE);
		}

		if (GizmoSubsystem)
		{
			if (GizmoBoundModel.Get() != SelectedModel)
			{
				GizmoSubsystem->SetTarget(SelectedModel->GetRootComponent());
				GizmoBoundModel = SelectedModel;
				GizmoBoundBuilding = nullptr;
			}
			else
			{
				GizmoSubsystem->UpdateGizmoTransform();
			}
		}

		if (!bIsGizmoAxisDrag)
		{
			float MouseX = 0.0f, MouseY = 0.0f;
			GetMousePosition(MouseX, MouseY);
			const FVector2D CurrentMousePos(MouseX, MouseY);
			if (FVector2D::DistSquared(CurrentMousePos, LastMousePosition) > 1.0f)
			{
				LastMousePosition = CurrentMousePos;
				if (GizmoSubsystem)
				{
					FGizmoInteractionRay Ray;
					if (BuildGizmoInteractionRay(Ray))
					{
						GizmoSubsystem->UpdateHover(Ray, GetCameraForwardVector());
					}
				}
			}
		}
	}
	else
	{
		if (ActiveMoveGizmo)
		{
			ActiveMoveGizmo->SetActorHiddenInGame(true);
			ActiveMoveGizmo->SetHighlightedSplinePoint(INDEX_NONE);
		}
		if (GizmoSubsystem && GizmoBoundBuilding.IsValid())
		{
			GizmoSubsystem->ClearTarget();
			GizmoBoundBuilding = nullptr;
		}
		if (GizmoSubsystem && GizmoBoundModel.IsValid())
		{
			GizmoSubsystem->ClearTarget();
			GizmoBoundModel = nullptr;
		}
	}
}

void AGRTEditController::UpdateHolographicPreview()
{
	if (!IsLocalController() || !IsInstructor())
	{
		if (HolographicGhostBuilding)
		{
			HolographicGhostBuilding->SetActorHiddenInGame(true);
		}
		return;
	}

	UGRTToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI || ToolUI->CurrentTool != EGRTToolMode::PlaceBuilding)
	{
		if (HolographicGhostBuilding)
		{
			HolographicGhostBuilding->SetActorHiddenInGame(true);
		}
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit) || !Hit.bBlockingHit)
	{
		if (HolographicGhostBuilding)
		{
			HolographicGhostBuilding->SetActorHiddenInGame(true);
		}
		return;
	}

	if (!HolographicGhostBuilding)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		HolographicGhostBuilding = GetWorld()->SpawnActor<AGRTBuildingActor>(
			AGRTBuildingActor::StaticClass(),
			FTransform(Hit.Location),
			SpawnParams);

		if (HolographicGhostBuilding)
		{
			HolographicGhostBuilding->SetIsHolographicPreview(true);
		}
	}

	if (HolographicGhostBuilding)
	{
		HolographicGhostBuilding->SetActorHiddenInGame(false);
		const int32 PendingFloors = FMath::Max(1, ToolUI->PendingFloorCount);
		if (HolographicGhostBuilding->FloorCount != PendingFloors)
		{
			HolographicGhostBuilding->SetFloorCount(PendingFloors);
		}
		HolographicGhostBuilding->SetActorLocation(Hit.Location);
		HolographicGhostBuilding->SnapToPadHeight();
		HolographicGhostBuilding->ApplyHolographicVisuals();
	}
}

void AGRTEditController::ApplyAndBroadcast(const FGRTBrushParams& Params)
{
	AGRTManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	Manager->ApplyGlobalBrush(Params.WorldLocation, Params.Radius, Params.Strength, Params.BrushMode, Params.FlattenHeight);

	// Mirror to remote clients. Multicast_ApplyBrush no-ops on the authority, which has just applied
	// the edit above, so this does not double-apply here.
	Manager->Multicast_ApplyBrush(Params);
}

void AGRTEditController::RequestBeginStroke(FVector WorldLocation, float Radius)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->BeginStroke(WorldLocation, Radius, this);
		}
	}
	else
	{
		Server_BeginStroke(WorldLocation, Radius);
	}
}

void AGRTEditController::Server_BeginStroke_Implementation(FVector WorldLocation, float Radius)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	if (AGRTManager* Manager = GetManager())
	{
		Manager->BeginStroke(WorldLocation, Radius, this);
	}
}

void AGRTEditController::RequestBrush(const FGRTBrushParams& Params)
{
	if (HasAuthority())
	{
		ApplyAndBroadcast(Params);
	}
	else
	{
		Server_ApplyBrush(Params);
	}
}

void AGRTEditController::Server_ApplyBrush_Implementation(const FGRTBrushParams& Params)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	ApplyAndBroadcast(Params);
}

void AGRTEditController::RequestCommitStroke()
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->CommitStroke(this);
		}
	}
	else
	{
		Server_CommitStroke();
	}
}

void AGRTEditController::Server_CommitStroke_Implementation()
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	if (AGRTManager* Manager = GetManager())
	{
		Manager->CommitStroke(this);
	}
}

void AGRTEditController::RequestUndo()
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->Undo(this);
		}
	}
	else
	{
		Server_Undo();
	}
}

void AGRTEditController::Server_Undo_Implementation()
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	if (AGRTManager* Manager = GetManager())
	{
		Manager->Undo(this);
	}
}

void AGRTEditController::RequestRedo()
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->Redo(this);
		}
	}
	else
	{
		Server_Redo();
	}
}

void AGRTEditController::Server_Redo_Implementation()
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	if (AGRTManager* Manager = GetManager())
	{
		Manager->Redo(this);
	}
}

void AGRTEditController::Client_ReceiveChunkSync_Implementation(const FGRTChunkData& Data)
{
	AGRTManager* Manager = GetManager();
	AGRTChunk* Chunk = Manager ? Manager->GetChunkAtCoord(Data.Coordinate) : nullptr;

	if (Chunk)
	{
		Chunk->LoadFromData(Data);
	}
	else
	{
		// Streaming has not spawned this chunk locally yet — Tick retries until it exists.
		PendingChunkSyncs.Add(Data.Coordinate, Data);
	}
}

void AGRTEditController::RequestPlaceBuilding(TSubclassOf<AGRTBuildingActor> BuildingClass, const FTransform& WorldTransform, int32 FloorCount)
{
	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (!World) return;

		TSubclassOf<AGRTBuildingActor> ClassToSpawn = BuildingClass ? BuildingClass : TSubclassOf<AGRTBuildingActor>(AGRTBuildingActor::StaticClass());
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGRTBuildingActor* NewBuilding = World->SpawnActor<AGRTBuildingActor>(ClassToSpawn, WorldTransform, SpawnParams);
		if (NewBuilding)
		{
			NewBuilding->SetFloorCount(FMath::Max(1, FloorCount));
			NewBuilding->SnapToPadHeight();
			if (UGRTToolWidget* ToolUI = GetToolWidget())
			{
				ToolUI->SelectedBuilding = NewBuilding;
			}

			FGRTActorSnapshot Snap;
			Snap.Kind = EGRTActorSnapshotKind::Building;
			Snap.BuildingData = NewBuilding->ToData();
			Snap.LiveActor = NewBuilding;
			FGRTActorUndoEntry UndoEntry;
			UndoEntry.Op = EGRTActorUndoOp::Spawn;
			UndoEntry.Actors.Add(MoveTemp(Snap));
			PushActorUndoEntry(MoveTemp(UndoEntry));
		}
	}
	else
	{
		Server_PlaceBuilding(BuildingClass, WorldTransform, FloorCount);
	}
}

void AGRTEditController::Server_PlaceBuilding_Implementation(TSubclassOf<AGRTBuildingActor> BuildingClass, FTransform WorldTransform, int32 FloorCount)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestPlaceBuilding(BuildingClass, WorldTransform, FloorCount);
}

void AGRTEditController::RequestMoveBuilding(AGRTBuildingActor* Building, const FTransform& NewTransform)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->SetActorTransform(NewTransform);
		Building->PadTargetWorldZ = NewTransform.GetLocation().Z;
		if (Building->bAutoPadHeight)
		{
			Building->SnapToPadHeight();
		}
		Building->NotifyBuildingChanged();
	}
	else
	{
		Server_MoveBuilding(Building, NewTransform);
	}
}

void AGRTEditController::Server_MoveBuilding_Implementation(AGRTBuildingActor* Building, FTransform NewTransform)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestMoveBuilding(Building, NewTransform);
}

void AGRTEditController::RequestSetBuildingFloorCount(AGRTBuildingActor* Building, int32 NewFloorCount)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->SetFloorCount(NewFloorCount);
	}
	else
	{
		Server_SetBuildingFloorCount(Building, NewFloorCount);
	}
}

void AGRTEditController::Server_SetBuildingFloorCount_Implementation(AGRTBuildingActor* Building, int32 NewFloorCount)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestSetBuildingFloorCount(Building, NewFloorCount);
}

void AGRTEditController::RequestSetBuildingSplinePoints(AGRTBuildingActor* Building, const TArray<FVector>& NewPoints)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->SetSplinePoints(NewPoints);
	}
	else
	{
		Server_SetBuildingSplinePoints(Building, NewPoints);
	}
}

void AGRTEditController::Server_SetBuildingSplinePoints_Implementation(AGRTBuildingActor* Building, const TArray<FVector>& NewPoints)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestSetBuildingSplinePoints(Building, NewPoints);
}

void AGRTEditController::RequestAddSplinePoint(AGRTBuildingActor* Building)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->AddSplinePoint();
	}
	else
	{
		Server_AddSplinePoint(Building);
	}
}

void AGRTEditController::Server_AddSplinePoint_Implementation(AGRTBuildingActor* Building)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestAddSplinePoint(Building);
}

void AGRTEditController::RequestRemoveSplinePoint(AGRTBuildingActor* Building, int32 PointIndex)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->RemoveSplinePoint(PointIndex);
	}
	else
	{
		Server_RemoveSplinePoint(Building, PointIndex);
	}
}

void AGRTEditController::Server_RemoveSplinePoint_Implementation(AGRTBuildingActor* Building, int32 PointIndex)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestRemoveSplinePoint(Building, PointIndex);
}

void AGRTEditController::RequestResetFootprint(AGRTBuildingActor* Building)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->ResetToDefaultFootprint();
	}
	else
	{
		Server_ResetFootprint(Building);
	}
}

void AGRTEditController::Server_ResetFootprint_Implementation(AGRTBuildingActor* Building)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestResetFootprint(Building);
}

void AGRTEditController::RequestRelevelBuilding(AGRTBuildingActor* Building)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->RelevelToTerrain();
		Building->SnapToPadHeight();
	}
	else
	{
		Server_RelevelBuilding(Building);
	}
}

void AGRTEditController::Server_RelevelBuilding_Implementation(AGRTBuildingActor* Building)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestRelevelBuilding(Building);
}

void AGRTEditController::RequestDeleteBuilding(AGRTBuildingActor* Building)
{
	if (!Building) return;
	if (HasAuthority())
	{
		FGRTActorSnapshot Snap;
		Snap.Kind = EGRTActorSnapshotKind::Building;
		Snap.BuildingData = Building->ToData(); // taken before Destroy() -- this IS the restore payload
		FGRTActorUndoEntry UndoEntry;
		UndoEntry.Op = EGRTActorUndoOp::Delete;
		UndoEntry.Actors.Add(MoveTemp(Snap));
		PushActorUndoEntry(MoveTemp(UndoEntry));

		Building->Destroy();
	}
	else
	{
		Server_DeleteBuilding(Building);
	}
}

void AGRTEditController::Server_DeleteBuilding_Implementation(AGRTBuildingActor* Building)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestDeleteBuilding(Building);
}

void AGRTEditController::RequestHarvestPopulation(const FGuid& PopulationId)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->HarvestPersistentPopulation(PopulationId);
		}
	}
	else
	{
		Server_HarvestPopulation(PopulationId);
	}
}

void AGRTEditController::Server_HarvestPopulation_Implementation(FGuid PopulationId)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestHarvestPopulation(PopulationId);
}

void AGRTEditController::RequestSetPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->SetPersistentPopulationDestroyed(PopulationId, bDestroyed);
		}
	}
	else
	{
		Server_SetPopulationDestroyed(PopulationId, bDestroyed);
	}
}

void AGRTEditController::Server_SetPopulationDestroyed_Implementation(FGuid PopulationId, bool bDestroyed)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestSetPopulationDestroyed(PopulationId, bDestroyed);
}

void AGRTEditController::RequestSaveWorldToFile(const FString& FilePath)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->SaveWorldToFile(FilePath);
		}
	}
	else
	{
		Server_SaveWorldToFile(FilePath);
	}
}

void AGRTEditController::Server_SaveWorldToFile_Implementation(const FString& FilePath)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestSaveWorldToFile(FilePath);
}

void AGRTEditController::RequestLoadWorldFromFile(const FString& FilePath)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->LoadWorldFromFile(FilePath);
		}
	}
	else
	{
		Server_LoadWorldFromFile(FilePath);
	}
}

void AGRTEditController::Server_LoadWorldFromFile_Implementation(const FString& FilePath)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestLoadWorldFromFile(FilePath);
}

void AGRTEditController::RequestImportGeoTIFF(const FString& FilePath)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->ImportGeoTIFF(FilePath);
		}
	}
	else
	{
		Server_ImportGeoTIFF(FilePath);
	}
}

void AGRTEditController::Server_ImportGeoTIFF_Implementation(const FString& FilePath)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestImportGeoTIFF(FilePath);
}

void AGRTEditController::RequestImportRasterLayer(EGRTRasterLayer Layer, const FString& FilePath)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			FString OutError;
			Manager->ImportRasterLayer(Layer, FilePath, OutError);
		}
	}
	else
	{
		Server_ImportRasterLayer(Layer, FilePath);
	}
}

void AGRTEditController::Server_ImportRasterLayer_Implementation(EGRTRasterLayer Layer, const FString& FilePath)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestImportRasterLayer(Layer, FilePath);
}

void AGRTEditController::RequestUnloadRasterLayer(EGRTRasterLayer Layer)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->UnloadRasterLayer(Layer);
		}
	}
	else
	{
		Server_UnloadRasterLayer(Layer);
	}
}

void AGRTEditController::Server_UnloadRasterLayer_Implementation(EGRTRasterLayer Layer)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestUnloadRasterLayer(Layer);
}

void AGRTEditController::RequestUnloadAllRasterLayers()
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->UnloadAllRasterLayers();
		}
	}
	else
	{
		Server_UnloadAllRasterLayers();
	}
}

void AGRTEditController::Server_UnloadAllRasterLayers_Implementation()
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestUnloadAllRasterLayers();
}

void AGRTEditController::RequestLoadRasterLayersIntoScene()
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			FString OutError;
			Manager->LoadRasterLayersIntoScene(OutError);
		}
	}
	else
	{
		Server_LoadRasterLayersIntoScene();
	}
}

void AGRTEditController::Server_LoadRasterLayersIntoScene_Implementation()
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestLoadRasterLayersIntoScene();
}

void AGRTEditController::RequestLoadSpecificRasterLayer(EGRTRasterLayer Layer)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			FString OutError;
			Manager->LoadSpecificRasterLayer(Layer, OutError);
		}
	}
	else
	{
		Server_LoadSpecificRasterLayer(Layer);
	}
}

void AGRTEditController::Server_LoadSpecificRasterLayer_Implementation(EGRTRasterLayer Layer)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestLoadSpecificRasterLayer(Layer);
}

void AGRTEditController::RequestUnloadSpecificRasterLayer(EGRTRasterLayer Layer)
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			FString OutError;
			Manager->UnloadSpecificRasterLayer(Layer, OutError);
		}
	}
	else
	{
		Server_UnloadSpecificRasterLayer(Layer);
	}
}

void AGRTEditController::Server_UnloadSpecificRasterLayer_Implementation(EGRTRasterLayer Layer)
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestUnloadSpecificRasterLayer(Layer);
}

void AGRTEditController::RequestStartSandboxMode()
{
	if (HasAuthority())
	{
		if (AGRTManager* Manager = GetManager())
		{
			Manager->StartSandboxMode();
		}
	}
	else
	{
		Server_StartSandboxMode();
	}
}

void AGRTEditController::Server_StartSandboxMode_Implementation()
{
	GRT_REQUIRE_EDIT_AUTHORITY();

	RequestStartSandboxMode();
}

void AGRTEditController::UpdateHolographicModelPreview()
{
	if (!IsLocalController() || !IsInstructor())
	{
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorHiddenInGame(true);
		}
		return;
	}

	UGRTToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI || ToolUI->CurrentTool != EGRTToolMode::PlaceModel)
	{
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorHiddenInGame(true);
		}
		return;
	}

	UGRTAssetLibrarySubsystem* AssetLib = GetWorld() ? GetWorld()->GetSubsystem<UGRTAssetLibrarySubsystem>() : nullptr;
	if (!AssetLib) return;

	const FGuid SelectedAssetId = AssetLib->GetSelectedPlacementAsset();
	const FGRTImportedModelAsset* Asset = AssetLib->FindAsset(SelectedAssetId);
	if (!Asset)
	{
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorHiddenInGame(true);
		}
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit) || !Hit.bBlockingHit)
	{
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorHiddenInGame(true);
		}
		return;
	}

	if (!HolographicGhostModel)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		HolographicGhostModel = GetWorld()->SpawnActor<AGRTPlacedModelActor>(
			AGRTPlacedModelActor::StaticClass(),
			FTransform(Hit.Location),
			SpawnParams);
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorEnableCollision(false);
		}
	}

	if (HolographicGhostModel)
	{
		HolographicGhostModel->SetActorHiddenInGame(false);
		if (HolographicGhostModel->AssetId != Asset->AssetId)
		{
			HolographicGhostModel->SetModelAsset(*Asset);
		}
		HolographicGhostModel->SetActorLocation(Hit.Location);
	}
}

void AGRTEditController::HandleModelClick()
{
	UGRTToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI) return;

	FHitResult Hit;
	if (!TraceUnderCursor(Hit) || !Hit.bBlockingHit) return;

	// A click that reaches here in Select mode landed on something that was not a model (the model
	// case already returned from OnLeftClickStart), so it means "deselect" -- the same way clicking
	// bare terrain under the building Select tool drops the selected building.
	if (ToolUI->CurrentTool == EGRTToolMode::SelectModel)
	{
		ClearModelSelection();
		return;
	}

	if (ToolUI->CurrentTool != EGRTToolMode::PlaceModel)
	{
		return;
	}

	UGRTAssetLibrarySubsystem* AssetLib = GetWorld() ? GetWorld()->GetSubsystem<UGRTAssetLibrarySubsystem>() : nullptr;
	if (!AssetLib) return;

	const FGuid AssetId = AssetLib->GetSelectedPlacementAsset();
	if (!AssetId.IsValid()) return;

	RequestPlaceModel(AssetId, FTransform(Hit.Location));
}

void AGRTEditController::SelectModelActor(AGRTPlacedModelActor* ModelActor)
{
	UGRTToolWidget* ToolUI = GetToolWidget();
	if (ToolUI)
	{
		if (ToolUI->SelectedModelActor.IsValid() && ToolUI->SelectedModelActor.Get() != ModelActor)
		{
			ToolUI->SelectedModelActor->SetSelectionState(false);
		}
		ToolUI->SelectedModelActor = ModelActor;
		if (ToolUI->SelectedBuilding.IsValid())
		{
			ToolUI->SelectedBuilding = nullptr;
		}
	}

	if (ModelActor)
	{
		ModelActor->SetSelectionState(true);
		if (GizmoSubsystem)
		{
			GizmoSubsystem->SetTarget(ModelActor->GetRootComponent());
			GizmoBoundModel = ModelActor;
			GizmoBoundBuilding = nullptr;
		}
	}
	else
	{
		if (GizmoSubsystem && GizmoBoundModel.IsValid())
		{
			GizmoSubsystem->ClearTarget();
			GizmoBoundModel = nullptr;
		}
	}
}

void AGRTEditController::ClearModelSelection()
{
	UGRTToolWidget* ToolUI = GetToolWidget();
	if (ToolUI && ToolUI->SelectedModelActor.IsValid())
	{
		ToolUI->SelectedModelActor->SetSelectionState(false);
		ToolUI->SelectedModelActor = nullptr;
	}
	if (GizmoSubsystem && GizmoBoundModel.IsValid())
	{
		GizmoSubsystem->ClearTarget();
		GizmoBoundModel = nullptr;
	}
}

void AGRTEditController::RequestPlaceModel(const FGuid& AssetId, const FTransform& WorldTransform)
{
	if (!AssetId.IsValid()) return;

	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGRTPlacedModelActor* NewModel = World->SpawnActor<AGRTPlacedModelActor>(
				AGRTPlacedModelActor::StaticClass(),
				WorldTransform,
				SpawnParams);

			if (NewModel)
			{
				if (UGRTAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UGRTAssetLibrarySubsystem>())
				{
					if (const FGRTImportedModelAsset* Asset = AssetLib->FindAsset(AssetId))
					{
						NewModel->SetModelAsset(*Asset);
					}
				}
				SelectModelActor(NewModel);

				FGRTActorSnapshot Snap;
				Snap.Kind = EGRTActorSnapshotKind::Model;
				Snap.ModelData = NewModel->ToData();
				Snap.LiveActor = NewModel;
				FGRTActorUndoEntry UndoEntry;
				UndoEntry.Op = EGRTActorUndoOp::Spawn;
				UndoEntry.Actors.Add(MoveTemp(Snap));
				PushActorUndoEntry(MoveTemp(UndoEntry));
			}
		}
	}
	else
	{
		Server_PlaceModel(AssetId, WorldTransform);
	}
}

void AGRTEditController::Server_PlaceModel_Implementation(FGuid AssetId, FTransform WorldTransform)
{
	GRT_REQUIRE_EDIT_AUTHORITY();
	RequestPlaceModel(AssetId, WorldTransform);
}

void AGRTEditController::RequestMoveModel(AGRTPlacedModelActor* ModelActor, const FTransform& NewTransform)
{
	if (!ModelActor) return;
	if (HasAuthority())
	{
		ModelActor->SetActorTransform(NewTransform);
		ModelActor->NotifyScaleChanged();
	}
	else
	{
		Server_MoveModel(ModelActor, NewTransform);
	}
}

void AGRTEditController::Server_MoveModel_Implementation(AGRTPlacedModelActor* ModelActor, FTransform NewTransform)
{
	GRT_REQUIRE_EDIT_AUTHORITY();
	RequestMoveModel(ModelActor, NewTransform);
}

void AGRTEditController::RequestDeleteModel(AGRTPlacedModelActor* ModelActor)
{
	if (!ModelActor) return;
	if (HasAuthority())
	{
		if (UGRTToolWidget* ToolUI = GetToolWidget())
		{
			if (ToolUI->SelectedModelActor.Get() == ModelActor)
			{
				ClearModelSelection();
			}
		}

		FGRTActorSnapshot Snap;
		Snap.Kind = EGRTActorSnapshotKind::Model;
		Snap.ModelData = ModelActor->ToData(); // taken before Destroy()
		FGRTActorUndoEntry UndoEntry;
		UndoEntry.Op = EGRTActorUndoOp::Delete;
		UndoEntry.Actors.Add(MoveTemp(Snap));
		PushActorUndoEntry(MoveTemp(UndoEntry));

		ModelActor->Destroy();
	}
	else
	{
		Server_DeleteModel(ModelActor);
	}
}

void AGRTEditController::Server_DeleteModel_Implementation(AGRTPlacedModelActor* ModelActor)
{
	GRT_REQUIRE_EDIT_AUTHORITY();
	RequestDeleteModel(ModelActor);
}

//-----------------------------------------------------------------------------
// OpenStreetMap ingestion
//-----------------------------------------------------------------------------

FGRTOSMImportSummary AGRTEditController::GetLastOSMImportSummary() const
{
	return LastOSMImportSummary;
}

void AGRTEditController::RequestImportOSM(const FString& FilePath, bool bImportBuildings)
{
	if (HasAuthority())
	{
		if (IsOSMImportInProgress())
		{
			// Reported through the same summary the status line already reads, rather than a
			// silently-dropped click -- the UX rule is "never leave a button press unresponsive
			// without clear error messaging."
			FGRTOSMImportSummary Busy;
			Busy.ErrorMessage = TEXT("An OSM import is already in progress -- wait for it to finish before starting another.");
			LastOSMImportSummary = Busy;
			return;
		}

		FGRTOSMImportOptions Options;
		Options.bImportBuildings = bImportBuildings;
		LastOSMImportSummary = ImportOSMInternal(FilePath, Options);
	}
	else
	{
		Server_ImportOSM(FilePath, bImportBuildings);
	}
}

void AGRTEditController::Server_ImportOSM_Implementation(const FString& FilePath, bool bImportBuildings)
{
	GRT_REQUIRE_EDIT_AUTHORITY();
	RequestImportOSM(FilePath, bImportBuildings);
}

FGRTOSMImportSummary AGRTEditController::ImportOSMInternal(const FString& FilePath, const FGRTOSMImportOptions& Options)
{
	FGRTOSMImportSummary Summary;

	UWorld* World = GetWorld();
	if (!World)
	{
		Summary.ErrorMessage = TEXT("No world to import into.");
		return Summary;
	}

	const FGRTOSMData Data = FGRTOSMParser::ParseFile(FilePath);
	if (!Data.bSuccess)
	{
		Summary.ErrorMessage = Data.ErrorMessage;
		return Summary;
	}

	// Everything in an OSM document is lon/lat, so the whole import hangs off the level's Cesium
	// georeference. Without one there is no defined mapping to world space and guessing would put
	// the city somewhere arbitrary.
	ACesiumGeoreference* Georeference = ACesiumGeoreference::GetDefaultGeoreference(World);
	if (!Georeference)
	{
		Summary.ErrorMessage = TEXT("No CesiumGeoreference in this level -- OSM data is longitude/latitude and cannot be placed without one.");
		return Summary;
	}

	auto LonLatToWorld = [Georeference](double Lon, double Lat) -> FVector
	{
		// Height 0: the footprint is placed on the georeference plane and each building resolves its
		// own ground height from the terrain afterwards via SnapToPadHeight.
		return Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Lon, Lat, 0.0));
	};

	// Classification, coordinate conversion, and footprint winding/centroid are all CPU-only and
	// cheap even for thousands of ways -- none of it is what would hitch the game thread. Only the
	// engine-side cost (SpawnActor, wall/mesh generation, terrain-height sampling) is deferred to
	// ProcessOSMSpawnQueue's ticked drain below, mirroring how AGRTManager::ProcessStreamingQueues
	// budgets chunk spawns across ticks rather than introducing a new async/threading pattern into a
	// codebase that has neither.
	PendingOSMSpawnQueue.Reset();
	int32 FeaturesQueued = 0;

	if (Options.bImportBuildings)
	{
		for (const FGRTOSMWay& Way : Data.Ways)
		{
			if (!FGRTOSMParser::IsBuilding(Way))
			{
				continue;
			}

			if (FeaturesQueued >= Options.MaxFeatures)
			{
				Summary.DroppedToLimit++;
				continue;
			}

			TArray<FVector> WorldPoints;
			if (!FGRTOSMIngest::ResolveWayWorldPoints(Data, Way, LonLatToWorld, WorldPoints))
			{
				Summary.BuildingsSkipped++;
				continue;
			}

			FVector Origin = FVector::ZeroVector;
			const TArray<FVector> Local = FGRTOSMIngest::BuildFootprintLocal(WorldPoints, Origin);
			if (Local.Num() < 3)
			{
				Summary.BuildingsSkipped++;
				continue;
			}

			FGRTOSMSpawnJob Job;
			Job.Origin = Origin;
			Job.LocalFootprint = Local;
			Job.Levels = FGRTOSMParser::ResolveBuildingLevels(Way, Options.DefaultBuildingLevels);
			PendingOSMSpawnQueue.Add(MoveTemp(Job));
			++FeaturesQueued;
		}
	}

	OSMImportTotalJobs = PendingOSMSpawnQueue.Num();
	InProgressOSMSummary = Summary; // carries forward the Skipped/DroppedToLimit counts already known
	InProgressOSMUndoEntry = FGRTActorUndoEntry();
	InProgressOSMUndoEntry.Op = EGRTActorUndoOp::Spawn;
	InProgressOSMFilePath = FilePath;

	if (PendingOSMSpawnQueue.Num() == 0)
	{
		// Nothing matched, or everything that did was skipped. Finish immediately rather than
		// leaving a phantom "in progress" state -- an empty queue would never be seen by
		// ProcessOSMSpawnQueue's Tick-driven drain, since that only runs while the queue is non-empty.
		Summary.bSuccess = true;
		UE_LOG(LogGRT, Log, TEXT("GRT: OSM import of '%s' -- %s"), *FilePath, *Summary.ToDisplayString());
		return Summary;
	}

	Summary.bSuccess = true; // queued successfully; BuildingsCreated/RoadsCreated fill in as the queue drains
	UE_LOG(LogGRT, Log, TEXT("GRT: OSM import of '%s' -- %d feature(s) queued."), *FilePath, PendingOSMSpawnQueue.Num());
	return Summary;
}

void AGRTEditController::ProcessOSMSpawnQueue()
{
	if (!HasAuthority() || PendingOSMSpawnQueue.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		// The world went away mid-import (level teardown) -- drop the queue rather than spin on it
		// forever with nothing left to spawn into.
		PendingOSMSpawnQueue.Reset();
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const double StartTimeSec = FPlatformTime::Seconds();
	const double MaxBudgetSec = MaxOSMMillisecondsPerTick * 0.001;

	while (PendingOSMSpawnQueue.Num() > 0)
	{
		const FGRTOSMSpawnJob Job = PendingOSMSpawnQueue.Pop();

		AGRTBuildingActor* NewBuilding = World->SpawnActor<AGRTBuildingActor>(
			AGRTBuildingActor::StaticClass(), FTransform(Job.Origin), SpawnParams);
		if (NewBuilding)
		{
			NewBuilding->bFlattenTerrain = false; // Prevents triggering heavy terrain distance-field pad stamping thousands of times!
			NewBuilding->SetFloorCount(Job.Levels);
			NewBuilding->SetSplinePoints(Job.LocalFootprint);
			NewBuilding->SnapToPadHeight();

			InProgressOSMSummary.BuildingsCreated++;

			FGRTActorSnapshot Snap;
			Snap.Kind = EGRTActorSnapshotKind::Building;
			Snap.BuildingData = NewBuilding->ToData();
			Snap.LiveActor = NewBuilding;
			InProgressOSMUndoEntry.Actors.Add(MoveTemp(Snap));
		}
		else
		{
			InProgressOSMSummary.BuildingsSkipped++;
		}

		// Frame-time budgeting: stop draining when we've spent our budget for this tick
		if ((FPlatformTime::Seconds() - StartTimeSec) >= MaxBudgetSec)
		{
			break;
		}
	}

	if (PendingOSMSpawnQueue.Num() == 0)
	{
		InProgressOSMSummary.bSuccess = true;
		LastOSMImportSummary = InProgressOSMSummary;
		UE_LOG(LogGRT, Log, TEXT("GRT: OSM import of '%s' -- %s"), *InProgressOSMFilePath, *LastOSMImportSummary.ToDisplayString());

		// One undo entry for the whole import, not one per feature -- a single Ctrl+Z reverses
		// everything the import created.
		if (InProgressOSMUndoEntry.Actors.Num() > 0)
		{
			PushActorUndoEntry(MoveTemp(InProgressOSMUndoEntry));
		}
		InProgressOSMUndoEntry = FGRTActorUndoEntry();
		OSMImportTotalJobs = 0;
	}
}

float AGRTEditController::GetOSMImportProgress(int32& OutDone, int32& OutTotal) const
{
	OutTotal = OSMImportTotalJobs;
	if (OutTotal <= 0)
	{
		OutDone = 0;
		return 1.0f;
	}
	OutDone = OutTotal - PendingOSMSpawnQueue.Num();
	return static_cast<float>(OutDone) / static_cast<float>(OutTotal);
}

