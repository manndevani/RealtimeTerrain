// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "World/TerraDyneChunk.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Building/TerraDyneGizmoActor.h"
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Gizmo/RuntimeGizmoActor.h"
#include "Components/SplineComponent.h"
#include "TerraDyneModule.h"
#include "Engine/World.h"
#include "UI/TerraDyneToolWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
// Controller.h only forward-declares APlayerState; the rejection log upcasts it to UObject*.
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Camera/PlayerCameraManager.h"

ATerraDyneEditController::ATerraDyneEditController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;
	UIClass = UTerraDyneToolWidget::StaticClass();
	bReplicates = true;
}

void ATerraDyneEditController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATerraDyneEditController, bCanEditTerrain);
}

bool ATerraDyneEditController::IsInstructor() const
{
	// On a listen server the host is both local and authoritative; remote clients can edit if bCanEditTerrain is enabled
	return IsLocalController() && (HasAuthority() || bCanEditTerrain);
}

bool ATerraDyneEditController::HasEditAuthority() const
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
#define TERRADYNE_REQUIRE_EDIT_AUTHORITY()                                                      \
	if (!HasEditAuthority())                                                                    \
	{                                                                                           \
		UE_LOG(LogTerraDyne, Warning,                                                           \
			TEXT("Rejected %hs from '%s' — controller has no edit rights (bCanEditTerrain)."),  \
			__func__, *GetNameSafe(PlayerState));                                               \
		return;                                                                                 \
	}

ATerraDyneManager* ATerraDyneEditController::GetManager() const
{
	UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetTerrainManager() : nullptr;
}

void ATerraDyneEditController::BeginPlay()
{
	Super::BeginPlay();

	PrimaryActorTick.bCanEverTick = true;

	// A client joining an in-progress session has correct procedural terrain (same noise, same
	// seed) but none of the edits made before it connected. Push the authoritative state for every
	// live chunk so it catches up.
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
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
			// ATerraDyneManager::BeginPlay) -- this is what actually asks the player to pick Sandbox
			// or Import GIS File, drawn over the tool panel just added above.
			if (UTerraDyneToolWidget* ToolUI = Cast<UTerraDyneToolWidget>(ActiveUI))
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

void ATerraDyneEditController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HolographicGhostBuilding && !HolographicGhostBuilding->IsActorBeingDestroyed())
	{
		HolographicGhostBuilding->Destroy();
		HolographicGhostBuilding = nullptr;
	}
	if (ActiveMoveGizmo && !ActiveMoveGizmo->IsActorBeingDestroyed())
	{
		ActiveMoveGizmo->Destroy();
		ActiveMoveGizmo = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ATerraDyneEditController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ATerraDyneEditController::OnLeftClickStart);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ATerraDyneEditController::OnLeftClickStop);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATerraDyneEditController::OnCancelManipulationPressed);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ATerraDyneEditController::OnToggleGizmoSpacePressed);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ATerraDyneEditController::OnToggleGizmoSnappingPressed);
	// Ctrl+Z / Ctrl+Y instead of bare keys, so Z and Y stay available for other input. Shift+W/E/R
	// instead of bare keys for the same reason -- W/E are also the (unconditional, modifier-blind)
	// axis-mapping keys for camera forward/up movement, so ATerraDyneEditPawn separately suppresses
	// its own movement handlers while Shift is held, keeping Shift+W/E/R free of any camera nudge.
	{
		FInputKeyBinding UndoBinding(FInputChord(EKeys::Z, false, true, false, false), IE_Pressed);
		UndoBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ATerraDyneEditController::OnUndoPressed);
		InputComponent->KeyBindings.Add(MoveTemp(UndoBinding));

		FInputKeyBinding RedoBinding(FInputChord(EKeys::Y, false, true, false, false), IE_Pressed);
		RedoBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ATerraDyneEditController::OnRedoPressed);
		InputComponent->KeyBindings.Add(MoveTemp(RedoBinding));

		FInputKeyBinding TranslateModeBinding(FInputChord(EKeys::W, true, false, false, false), IE_Pressed);
		TranslateModeBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ATerraDyneEditController::OnSetGizmoModeTranslate);
		InputComponent->KeyBindings.Add(MoveTemp(TranslateModeBinding));

		FInputKeyBinding RotateModeBinding(FInputChord(EKeys::E, true, false, false, false), IE_Pressed);
		RotateModeBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ATerraDyneEditController::OnSetGizmoModeRotate);
		InputComponent->KeyBindings.Add(MoveTemp(RotateModeBinding));

		FInputKeyBinding ScaleModeBinding(FInputChord(EKeys::R, true, false, false, false), IE_Pressed);
		ScaleModeBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ATerraDyneEditController::OnSetGizmoModeScale);
		InputComponent->KeyBindings.Add(MoveTemp(ScaleModeBinding));
	}
}

UTerraDyneToolWidget* ATerraDyneEditController::GetToolWidget() const
{
	return Cast<UTerraDyneToolWidget>(ActiveUI);
}

bool ATerraDyneEditController::TraceUnderCursor(FHitResult& OutHit) const
{
	FVector WorldLoc, WorldDir;
	if (!const_cast<ATerraDyneEditController*>(this)->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
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

bool ATerraDyneEditController::TraceSplinePointUnderCursor(int32& OutPointIndex, FVector& OutHitPoint) const
{
	OutPointIndex = INDEX_NONE;
	if (!ActiveMoveGizmo || ActiveMoveGizmo->IsHidden())
	{
		return false;
	}

	FVector WorldLoc, WorldDir;
	if (!const_cast<ATerraDyneEditController*>(this)->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
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

bool ATerraDyneEditController::IntersectCursorWithPlane(const FPlane& Plane, FVector& OutIntersection) const
{
	FVector WorldLoc, WorldDir;
	if (!const_cast<ATerraDyneEditController*>(this)->DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}

	OutIntersection = FMath::RayPlaneIntersection(WorldLoc, WorldDir, Plane);
	return true;
}

bool ATerraDyneEditController::BuildGizmoInteractionRay(FGizmoInteractionRay& OutRay) const
{
	return const_cast<ATerraDyneEditController*>(this)->DeprojectMousePositionToWorld(OutRay.Origin, OutRay.Direction);
}

FVector ATerraDyneEditController::GetCameraForwardVector() const
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->GetCameraRotation().Vector();
	}
	FGizmoInteractionRay Ray;
	return BuildGizmoInteractionRay(Ray) ? Ray.Direction : FVector::ForwardVector;
}

void ATerraDyneEditController::OnLeftClickStart()
{
	bFlattenHeightLocked = false;

	UTerraDyneToolWidget* ToolUI = GetToolWidget();
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

	if (ToolUI && (IsBuildingToolMode(ToolUI->CurrentTool) || IsRoadToolMode(ToolUI->CurrentTool)))
	{
		// Deliberately does NOT set bIsClicking: that flag is what makes Tick keep re-applying the
		// action, which is right for a brush and wrong for placing an actor.
		if (IsBuildingToolMode(ToolUI->CurrentTool))
		{
			HandleBuildingClick();
		}
		else if (IsRoadToolMode(ToolUI->CurrentTool))
		{
			HandleRoadClick();
		}
		return;
	}

	bIsClicking = true;
}

void ATerraDyneEditController::HandleRoadClick()
{
	UTerraDyneToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI)
	{
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UClass* SplineRoadClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/RealtimeTerrain.RealtimeRoadSplineActor"));
	UClass* JunctionClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/RealtimeTerrain.RealtimeRoadJunctionActor"));

	if (ToolUI->CurrentTool == ETerraDyneToolMode::PlaceJunction)
	{
		if (JunctionClass)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<AActor>(JunctionClass, Hit.Location, FRotator::ZeroRotator, SpawnParams);
		}
		return;
	}

	if (ToolUI->CurrentTool == ETerraDyneToolMode::DrawPavedRoad || ToolUI->CurrentTool == ETerraDyneToolMode::DrawRoughRoad)
	{
		if (!SplineRoadClass)
		{
			return;
		}

		const uint8 TargetRoadType = (ToolUI->CurrentTool == ETerraDyneToolMode::DrawPavedRoad) ? 1 : 0; // 0 = Rough, 1 = Paved

		// 1. If we already have a selected road, extend it to the clicked destination!
		if (SelectedRoadActor.IsValid())
		{
			AActor* Road = SelectedRoadActor.Get();
			UFunction* ExtendFunc = Road->FindFunction(FName("ExtendFromPointToLocation"));
			if (ExtendFunc)
			{
				// This struct's layout must mirror ARealtimeRoadSplineActor::ExtendFromPointToLocation's
				// UFUNCTION signature exactly (params in order, then the return value last) -- this
				// plugin can't include the game module's RealtimeRoadSplineActor.h (wrong dependency
				// direction), so the call has to go through reflection instead of a typed pointer.
				// ReturnValue is deliberately AActor*, not the real return type: a pointer's size and
				// layout don't depend on its static type, and this code never needs to call anything
				// on it -- it only has to occupy the right slot so ProcessEvent doesn't write past
				// the end of this struct.
				struct FExtendParams
				{
					int32 FromPointIndex;
					FVector TargetWorldLocation;
					int32 OutSelectedPointIndex;
					float MaxSegmentSpacing;
					AActor* ReturnValue;
				};
				FExtendParams Params;
				Params.FromPointIndex = SelectedRoadPointIndex != INDEX_NONE ? SelectedRoadPointIndex : 0;
				Params.TargetWorldLocation = Hit.Location;
				Params.OutSelectedPointIndex = INDEX_NONE;
				Params.MaxSegmentSpacing = 800.0f;
				Params.ReturnValue = nullptr;
				Road->ProcessEvent(ExtendFunc, &Params);

				// Extending from an interior point branches into a new road -- follow the selection
				// there instead of leaving it pointed at the unchanged original.
				if (Params.ReturnValue)
				{
					SelectedRoadActor = Params.ReturnValue;
				}
				if (Params.OutSelectedPointIndex != INDEX_NONE)
				{
					SelectedRoadPointIndex = Params.OutSelectedPointIndex;
				}
			}
			return;
		}

		// 2. Otherwise: spawn a brand new road at the clicked location
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* NewRoad = World->SpawnActor<AActor>(SplineRoadClass, Hit.Location, FRotator::ZeroRotator, SpawnParams);
		if (NewRoad)
		{
			// Set RoadType byte/enum property
			if (FByteProperty* TypeProp = CastField<FByteProperty>(NewRoad->GetClass()->FindPropertyByName(FName("RoadType"))))
			{
				TypeProp->SetPropertyValue_InContainer(NewRoad, TargetRoadType);
			}
			else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(NewRoad->GetClass()->FindPropertyByName(FName("RoadType"))))
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(NewRoad), static_cast<int64>(TargetRoadType));
			}

			// Add initial point
			UFunction* SetPointsFunc = NewRoad->FindFunction(FName("SetPointsRealtime"));
			if (SetPointsFunc)
			{
				struct FSetPointsParams
				{
					TArray<FVector> WorldPoints;
				};
				FSetPointsParams Params;
				Params.WorldPoints.Add(Hit.Location);
				NewRoad->ProcessEvent(SetPointsFunc, &Params);
			}

			SelectedRoadActor = NewRoad;
			SelectedRoadPointIndex = 0;
		}
	}
}

void ATerraDyneEditController::HandleBuildingClick()
{
	UTerraDyneToolWidget* ToolUI = GetToolWidget();
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
	if (ATerraDyneBuildingActor* HitBuilding = Cast<ATerraDyneBuildingActor>(HitActor))
	{
		ToolUI->SelectedBuilding = HitBuilding;
		ToolUI->PendingFloorCount = HitBuilding->FloorCount;
		return;
	}

	if (ToolUI->CurrentTool != ETerraDyneToolMode::PlaceBuilding)
	{
		ToolUI->SelectedBuilding = nullptr;
		return;
	}

	const int32 FloorCount = FMath::Max(1, ToolUI->PendingFloorCount);
	RequestPlaceBuilding(ATerraDyneBuildingActor::StaticClass(), FTransform(Hit.Location), FloorCount);
}

void ATerraDyneEditController::OnLeftClickStop()
{
	bIsClicking = false;
	bFlattenHeightLocked = false;

	// Road tools get the release too, so a subclass can end a point drag. Road and building modes
	// are mutually exclusive, and a road press never begins a stroke, so there is nothing below
	// this that a road release needs to fall through to.
	if (const UTerraDyneToolWidget* RoadToolUI = GetToolWidget())
	{
		if (IsRoadToolMode(RoadToolUI->CurrentTool))
		{
			HandleRoadRelease();
			return;
		}
	}

	if (DraggingSplinePointIndex != INDEX_NONE)
	{
		UTerraDyneToolWidget* ToolUI = GetToolWidget();
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
		UTerraDyneToolWidget* ToolUI = GetToolWidget();
		if (ToolUI && ToolUI->SelectedBuilding.IsValid())
		{
			RequestMoveBuilding(ToolUI->SelectedBuilding.Get(), ToolUI->SelectedBuilding->GetActorTransform());
		}
		return;
	}

	if (bStrokeBegun)
	{
		bStrokeBegun = false;
		RequestCommitStroke();
	}
}

void ATerraDyneEditController::OnUndoPressed()
{
	// Road history first. Only fall through to terrain when there was no road edit to take back.
	if (!TryRoadUndo())
	{
		RequestUndo();
	}
}
void ATerraDyneEditController::OnRedoPressed()
{
	if (!TryRoadRedo())
	{
		RequestRedo();
	}
}

void ATerraDyneEditController::OnCancelManipulationPressed()
{
	// One call serves both consumers -- the subsystem already knows which one (if either) is
	// actually mid-drag, and CancelManipulation is a no-op if nothing is.
	if (GizmoSubsystem)
	{
		GizmoSubsystem->CancelManipulation();
	}
}

void ATerraDyneEditController::OnSetGizmoModeTranslate()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetMode(EGizmoMode::Translate);
	}
}

void ATerraDyneEditController::OnSetGizmoModeRotate()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetMode(EGizmoMode::Rotate);
	}
}

void ATerraDyneEditController::OnSetGizmoModeScale()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetMode(EGizmoMode::Scale);
	}
}

void ATerraDyneEditController::OnToggleGizmoSpacePressed()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetCoordinateSpace(
			GizmoSubsystem->GetCoordinateSpace() == EGizmoCoordinateSpace::World
				? EGizmoCoordinateSpace::Local
				: EGizmoCoordinateSpace::World);
	}
}

void ATerraDyneEditController::OnToggleGizmoSnappingPressed()
{
	if (GizmoSubsystem)
	{
		GizmoSubsystem->SetSnappingEnabled(!GizmoSubsystem->IsSnappingEnabled());
	}
}

void ATerraDyneEditController::HandleGizmoManipulationCancelled(USceneComponent* CancelledTarget)
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
	ATerraDyneBuildingActor* Building = GizmoBoundBuilding.Get();
	Building->PadTargetWorldZ = Building->GetActorLocation().Z;
	Building->NotifyBuildingChanged();

	if (ActiveMoveGizmo)
	{
		ActiveMoveGizmo->SetActorLocation(Building->GetActorLocation());
		ActiveMoveGizmo->UpdateSplinePointHandles(Building->GatherSplinePointsLocal(), Building->GetActorTransform());
	}
}

void ATerraDyneEditController::PerformToolAction()
{
	float Radius = 2000.0f;
	float Strength = 1000.0f;
	ETerraDyneToolMode Mode = ETerraDyneToolMode::SculptRaise;
	if (UTerraDyneToolWidget* ToolUI = GetToolWidget())
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

	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	if (!bStrokeBegun)
	{
		bStrokeBegun = true;
		RequestBeginStroke(Hit.Location, Radius);
	}

	ETerraDyneBrushMode BrushMode = ETerraDyneBrushMode::Raise;
	if (Mode == ETerraDyneToolMode::SculptLower)
	{
		BrushMode = ETerraDyneBrushMode::Lower;
		Strength = -FMath::Abs(Strength);
	}
	else if (Mode == ETerraDyneToolMode::Flatten)
	{
		BrushMode = ETerraDyneBrushMode::Flatten;
		Strength = FMath::Abs(Strength);
	}
	else if (Mode == ETerraDyneToolMode::Smooth)
	{
		BrushMode = ETerraDyneBrushMode::Smooth;
		Strength = FMath::Abs(Strength);
	}

	float FlattenHeight = 0.0f;
	if (BrushMode == ETerraDyneBrushMode::Flatten)
	{
		if (!bFlattenHeightLocked)
		{
			if (ATerraDyneChunk* HitChunk = Manager->GetChunkAtLocation(Hit.Location))
			{
				LockedFlattenHeight = HitChunk->GetHeightAtLocation(Hit.Location - HitChunk->GetActorLocation());
			}
			bFlattenHeightLocked = true;
		}
		FlattenHeight = LockedFlattenHeight;
	}

	FTerraDyneBrushParams BrushParams;
	BrushParams.WorldLocation = Hit.Location;
	BrushParams.Radius = Radius;
	BrushParams.Strength = Strength;
	BrushParams.BrushMode = BrushMode;
	BrushParams.FlattenHeight = FlattenHeight;
	RequestBrush(BrushParams);

	DrawDebugSphere(GetWorld(), Hit.Location, Radius, 16, FColor::Green, false, -1.0f, 0, 2.0f);
}

void ATerraDyneEditController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TimeSinceLastPadUpdate += DeltaTime;

	// 1. Handle spline point vertex drag
	if (DraggingSplinePointIndex != INDEX_NONE)
	{
		UTerraDyneToolWidget* ToolUI = GetToolWidget();
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
		UTerraDyneToolWidget* ToolUI = GetToolWidget();
		if (ToolUI && ToolUI->SelectedBuilding.IsValid() && GizmoSubsystem)
		{
			FGizmoInteractionRay Ray;
			if (BuildGizmoInteractionRay(Ray))
			{
				const bool bInvertSnap = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
				GizmoSubsystem->UpdateManipulation(Ray, GetCameraForwardVector(), bInvertSnap);

				ToolUI->SelectedBuilding->PadTargetWorldZ = ToolUI->SelectedBuilding->GetActorLocation().Z;

				// Throttled terrain pad update during continuous drag (~20Hz)
				if (TimeSinceLastPadUpdate >= 0.05f)
				{
					TimeSinceLastPadUpdate = 0.0f;
					ToolUI->SelectedBuilding->SnapToPadHeight();
					ToolUI->SelectedBuilding->NotifyBuildingChanged();
				}

				if (ActiveMoveGizmo)
				{
					ActiveMoveGizmo->SetActorLocation(ToolUI->SelectedBuilding->GetActorLocation());
					ActiveMoveGizmo->UpdateSplinePointHandles(ToolUI->SelectedBuilding->GatherSplinePointsLocal(), ToolUI->SelectedBuilding->GetActorTransform());
				}
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

	if (PendingChunkSyncs.Num() == 0)
	{
		return;
	}

	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	TArray<FIntPoint> Resolved;
	for (const TPair<FIntPoint, FTerraDyneChunkData>& Pair : PendingChunkSyncs)
	{
		if (ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Pair.Key))
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

void ATerraDyneEditController::UpdateMoveGizmo()
{
	if (!GizmoSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			GizmoSubsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
			if (GizmoSubsystem)
			{
				GizmoSubsystem->OnManipulationCancel.AddUObject(this, &ATerraDyneEditController::HandleGizmoManipulationCancelled);
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

	UTerraDyneToolWidget* ToolUI = GetToolWidget();
	ATerraDyneBuildingActor* Selected = ToolUI ? ToolUI->SelectedBuilding.Get() : nullptr;

	if (Selected && !Selected->IsActorBeingDestroyed())
	{
		if (!ActiveMoveGizmo)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;
			ActiveMoveGizmo = GetWorld()->SpawnActor<ATerraDyneGizmoActor>(
				ATerraDyneGizmoActor::StaticClass(),
				FTransform(Selected->GetActorLocation()),
				SpawnParams);
		}

		if (ActiveMoveGizmo)
		{
			ActiveMoveGizmo->SetActorLocation(Selected->GetActorLocation());
			ActiveMoveGizmo->SetActorHiddenInGame(false);
			ActiveMoveGizmo->UpdateSplinePointHandles(Selected->GatherSplinePointsLocal(), Selected->GetActorTransform());
		}

		if (GizmoSubsystem && GizmoBoundBuilding.Get() != Selected)
		{
			GizmoSubsystem->SetTarget(Selected->GetRootComponent());
			GizmoBoundBuilding = Selected;
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
	}
}

void ATerraDyneEditController::UpdateHolographicPreview()
{
	if (!IsLocalController() || !IsInstructor())
	{
		if (HolographicGhostBuilding)
		{
			HolographicGhostBuilding->SetActorHiddenInGame(true);
		}
		return;
	}

	UTerraDyneToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI || ToolUI->CurrentTool != ETerraDyneToolMode::PlaceBuilding)
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
		HolographicGhostBuilding = GetWorld()->SpawnActor<ATerraDyneBuildingActor>(
			ATerraDyneBuildingActor::StaticClass(),
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

void ATerraDyneEditController::ApplyAndBroadcast(const FTerraDyneBrushParams& Params)
{
	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	Manager->ApplyGlobalBrush(Params.WorldLocation, Params.Radius, Params.Strength, Params.BrushMode, Params.FlattenHeight);

	// Mirror to remote clients. Multicast_ApplyBrush no-ops on the authority, which has just applied
	// the edit above, so this does not double-apply here.
	Manager->Multicast_ApplyBrush(Params);
}

void ATerraDyneEditController::RequestBeginStroke(FVector WorldLocation, float Radius)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->BeginStroke(WorldLocation, Radius, this);
		}
	}
	else
	{
		Server_BeginStroke(WorldLocation, Radius);
	}
}

void ATerraDyneEditController::Server_BeginStroke_Implementation(FVector WorldLocation, float Radius)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->BeginStroke(WorldLocation, Radius, this);
	}
}

void ATerraDyneEditController::RequestBrush(const FTerraDyneBrushParams& Params)
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

void ATerraDyneEditController::Server_ApplyBrush_Implementation(const FTerraDyneBrushParams& Params)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	ApplyAndBroadcast(Params);
}

void ATerraDyneEditController::RequestCommitStroke()
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->CommitStroke(this);
		}
	}
	else
	{
		Server_CommitStroke();
	}
}

void ATerraDyneEditController::Server_CommitStroke_Implementation()
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->CommitStroke(this);
	}
}

void ATerraDyneEditController::RequestUndo()
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->Undo(this);
		}
	}
	else
	{
		Server_Undo();
	}
}

void ATerraDyneEditController::Server_Undo_Implementation()
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->Undo(this);
	}
}

void ATerraDyneEditController::RequestRedo()
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->Redo(this);
		}
	}
	else
	{
		Server_Redo();
	}
}

void ATerraDyneEditController::Server_Redo_Implementation()
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->Redo(this);
	}
}

void ATerraDyneEditController::Client_ReceiveChunkSync_Implementation(const FTerraDyneChunkData& Data)
{
	ATerraDyneManager* Manager = GetManager();
	ATerraDyneChunk* Chunk = Manager ? Manager->GetChunkAtCoord(Data.Coordinate) : nullptr;

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

void ATerraDyneEditController::RequestPlaceBuilding(TSubclassOf<ATerraDyneBuildingActor> BuildingClass, const FTransform& WorldTransform, int32 FloorCount)
{
	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (!World) return;

		TSubclassOf<ATerraDyneBuildingActor> ClassToSpawn = BuildingClass ? BuildingClass : TSubclassOf<ATerraDyneBuildingActor>(ATerraDyneBuildingActor::StaticClass());
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATerraDyneBuildingActor* NewBuilding = World->SpawnActor<ATerraDyneBuildingActor>(ClassToSpawn, WorldTransform, SpawnParams);
		if (NewBuilding)
		{
			NewBuilding->SetFloorCount(FMath::Max(1, FloorCount));
			NewBuilding->SnapToPadHeight();
			if (UTerraDyneToolWidget* ToolUI = GetToolWidget())
			{
				ToolUI->SelectedBuilding = NewBuilding;
			}
		}
	}
	else
	{
		Server_PlaceBuilding(BuildingClass, WorldTransform, FloorCount);
	}
}

void ATerraDyneEditController::Server_PlaceBuilding_Implementation(TSubclassOf<ATerraDyneBuildingActor> BuildingClass, FTransform WorldTransform, int32 FloorCount)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestPlaceBuilding(BuildingClass, WorldTransform, FloorCount);
}

void ATerraDyneEditController::RequestMoveBuilding(ATerraDyneBuildingActor* Building, const FTransform& NewTransform)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->SetActorTransform(NewTransform);
		Building->SnapToPadHeight();
		Building->NotifyBuildingChanged();
	}
	else
	{
		Server_MoveBuilding(Building, NewTransform);
	}
}

void ATerraDyneEditController::Server_MoveBuilding_Implementation(ATerraDyneBuildingActor* Building, FTransform NewTransform)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestMoveBuilding(Building, NewTransform);
}

void ATerraDyneEditController::RequestSetBuildingFloorCount(ATerraDyneBuildingActor* Building, int32 NewFloorCount)
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

void ATerraDyneEditController::Server_SetBuildingFloorCount_Implementation(ATerraDyneBuildingActor* Building, int32 NewFloorCount)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestSetBuildingFloorCount(Building, NewFloorCount);
}

void ATerraDyneEditController::RequestSetBuildingSplinePoints(ATerraDyneBuildingActor* Building, const TArray<FVector>& NewPoints)
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

void ATerraDyneEditController::Server_SetBuildingSplinePoints_Implementation(ATerraDyneBuildingActor* Building, const TArray<FVector>& NewPoints)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestSetBuildingSplinePoints(Building, NewPoints);
}

void ATerraDyneEditController::RequestAddSplinePoint(ATerraDyneBuildingActor* Building)
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

void ATerraDyneEditController::Server_AddSplinePoint_Implementation(ATerraDyneBuildingActor* Building)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestAddSplinePoint(Building);
}

void ATerraDyneEditController::RequestRemoveSplinePoint(ATerraDyneBuildingActor* Building, int32 PointIndex)
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

void ATerraDyneEditController::Server_RemoveSplinePoint_Implementation(ATerraDyneBuildingActor* Building, int32 PointIndex)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestRemoveSplinePoint(Building, PointIndex);
}

void ATerraDyneEditController::RequestResetFootprint(ATerraDyneBuildingActor* Building)
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

void ATerraDyneEditController::Server_ResetFootprint_Implementation(ATerraDyneBuildingActor* Building)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestResetFootprint(Building);
}

void ATerraDyneEditController::RequestRelevelBuilding(ATerraDyneBuildingActor* Building)
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

void ATerraDyneEditController::Server_RelevelBuilding_Implementation(ATerraDyneBuildingActor* Building)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestRelevelBuilding(Building);
}

void ATerraDyneEditController::RequestDeleteBuilding(ATerraDyneBuildingActor* Building)
{
	if (!Building) return;
	if (HasAuthority())
	{
		Building->Destroy();
	}
	else
	{
		Server_DeleteBuilding(Building);
	}
}

void ATerraDyneEditController::Server_DeleteBuilding_Implementation(ATerraDyneBuildingActor* Building)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestDeleteBuilding(Building);
}

void ATerraDyneEditController::RequestHarvestPopulation(const FGuid& PopulationId)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->HarvestPersistentPopulation(PopulationId);
		}
	}
	else
	{
		Server_HarvestPopulation(PopulationId);
	}
}

void ATerraDyneEditController::Server_HarvestPopulation_Implementation(FGuid PopulationId)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestHarvestPopulation(PopulationId);
}

void ATerraDyneEditController::RequestSetPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->SetPersistentPopulationDestroyed(PopulationId, bDestroyed);
		}
	}
	else
	{
		Server_SetPopulationDestroyed(PopulationId, bDestroyed);
	}
}

void ATerraDyneEditController::Server_SetPopulationDestroyed_Implementation(FGuid PopulationId, bool bDestroyed)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestSetPopulationDestroyed(PopulationId, bDestroyed);
}

void ATerraDyneEditController::RequestSaveWorldToFile(const FString& FilePath)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->SaveWorldToFile(FilePath);
		}
	}
	else
	{
		Server_SaveWorldToFile(FilePath);
	}
}

void ATerraDyneEditController::Server_SaveWorldToFile_Implementation(const FString& FilePath)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestSaveWorldToFile(FilePath);
}

void ATerraDyneEditController::RequestLoadWorldFromFile(const FString& FilePath)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->LoadWorldFromFile(FilePath);
		}
	}
	else
	{
		Server_LoadWorldFromFile(FilePath);
	}
}

void ATerraDyneEditController::Server_LoadWorldFromFile_Implementation(const FString& FilePath)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestLoadWorldFromFile(FilePath);
}

void ATerraDyneEditController::RequestImportGeoTIFF(const FString& FilePath)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->ImportGeoTIFF(FilePath);
		}
	}
	else
	{
		Server_ImportGeoTIFF(FilePath);
	}
}

void ATerraDyneEditController::Server_ImportGeoTIFF_Implementation(const FString& FilePath)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestImportGeoTIFF(FilePath);
}

void ATerraDyneEditController::RequestImportRasterLayer(ETerraDyneRasterLayer Layer, const FString& FilePath)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
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

void ATerraDyneEditController::Server_ImportRasterLayer_Implementation(ETerraDyneRasterLayer Layer, const FString& FilePath)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestImportRasterLayer(Layer, FilePath);
}

void ATerraDyneEditController::RequestUnloadRasterLayer(ETerraDyneRasterLayer Layer)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->UnloadRasterLayer(Layer);
		}
	}
	else
	{
		Server_UnloadRasterLayer(Layer);
	}
}

void ATerraDyneEditController::Server_UnloadRasterLayer_Implementation(ETerraDyneRasterLayer Layer)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestUnloadRasterLayer(Layer);
}

void ATerraDyneEditController::RequestUnloadAllRasterLayers()
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->UnloadAllRasterLayers();
		}
	}
	else
	{
		Server_UnloadAllRasterLayers();
	}
}

void ATerraDyneEditController::Server_UnloadAllRasterLayers_Implementation()
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestUnloadAllRasterLayers();
}

void ATerraDyneEditController::RequestLoadRasterLayersIntoScene()
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
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

void ATerraDyneEditController::Server_LoadRasterLayersIntoScene_Implementation()
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestLoadRasterLayersIntoScene();
}

void ATerraDyneEditController::RequestLoadSpecificRasterLayer(ETerraDyneRasterLayer Layer)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
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

void ATerraDyneEditController::Server_LoadSpecificRasterLayer_Implementation(ETerraDyneRasterLayer Layer)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestLoadSpecificRasterLayer(Layer);
}

void ATerraDyneEditController::RequestUnloadSpecificRasterLayer(ETerraDyneRasterLayer Layer)
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
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

void ATerraDyneEditController::Server_UnloadSpecificRasterLayer_Implementation(ETerraDyneRasterLayer Layer)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestUnloadSpecificRasterLayer(Layer);
}

void ATerraDyneEditController::RequestStartSandboxMode()
{
	if (HasAuthority())
	{
		if (ATerraDyneManager* Manager = GetManager())
		{
			Manager->StartSandboxMode();
		}
	}
	else
	{
		Server_StartSandboxMode();
	}
}

void ATerraDyneEditController::Server_StartSandboxMode_Implementation()
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();

	RequestStartSandboxMode();
}
