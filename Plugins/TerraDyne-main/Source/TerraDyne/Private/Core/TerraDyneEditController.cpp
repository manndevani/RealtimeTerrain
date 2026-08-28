// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "World/TerraDyneChunk.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Building/TerraDynePlacedModelActor.h"
#include "Building/TerraDyneGizmoActor.h"
#include "Core/TerraDyneAssetLibrarySubsystem.h"
#include "IO/TerraDyneOSMParser.h"
#include "IO/TerraDyneOSMIngest.h"
#include "CesiumGeoreference.h"
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
			if (ATerraDynePlacedModelActor* HitModel = Cast<ATerraDynePlacedModelActor>(ModelHit.GetActor()))
			{
				SelectModelActor(HitModel);
				return;
			}
		}
	}

	if (ToolUI && (IsBuildingToolMode(ToolUI->CurrentTool) || IsRoadToolMode(ToolUI->CurrentTool) || IsModelToolMode(ToolUI->CurrentTool)))
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
		else if (IsModelToolMode(ToolUI->CurrentTool))
		{
			HandleModelClick();
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

	// Fail-safe: if the left mouse button is no longer down, terminate any active gizmo/vertex/stroke drag
	// so the actor or gizmo never gets stuck to the mouse if a release event was consumed by UI or missed.
	if ((bIsGizmoAxisDrag || DraggingSplinePointIndex != INDEX_NONE || bIsClicking) && !IsInputKeyDown(EKeys::LeftMouseButton))
	{
		OnLeftClickStop();
	}

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
	else if (ATerraDynePlacedModelActor* SelectedModel = ToolUI ? ToolUI->SelectedModelActor.Get() : nullptr)
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

void ATerraDyneEditController::UpdateHolographicModelPreview()
{
	if (!IsLocalController() || !IsInstructor())
	{
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorHiddenInGame(true);
		}
		return;
	}

	UTerraDyneToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI || ToolUI->CurrentTool != ETerraDyneToolMode::PlaceModel)
	{
		if (HolographicGhostModel)
		{
			HolographicGhostModel->SetActorHiddenInGame(true);
		}
		return;
	}

	UTerraDyneAssetLibrarySubsystem* AssetLib = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneAssetLibrarySubsystem>() : nullptr;
	if (!AssetLib) return;

	const FGuid SelectedAssetId = AssetLib->GetSelectedPlacementAsset();
	const FTerraDyneImportedModelAsset* Asset = AssetLib->FindAsset(SelectedAssetId);
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
		HolographicGhostModel = GetWorld()->SpawnActor<ATerraDynePlacedModelActor>(
			ATerraDynePlacedModelActor::StaticClass(),
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

void ATerraDyneEditController::HandleModelClick()
{
	UTerraDyneToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI) return;

	FHitResult Hit;
	if (!TraceUnderCursor(Hit) || !Hit.bBlockingHit) return;

	// A click that reaches here in Select mode landed on something that was not a model (the model
	// case already returned from OnLeftClickStart), so it means "deselect" -- the same way clicking
	// bare terrain under the building Select tool drops the selected building.
	if (ToolUI->CurrentTool == ETerraDyneToolMode::SelectModel)
	{
		ClearModelSelection();
		return;
	}

	if (ToolUI->CurrentTool != ETerraDyneToolMode::PlaceModel)
	{
		return;
	}

	UTerraDyneAssetLibrarySubsystem* AssetLib = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneAssetLibrarySubsystem>() : nullptr;
	if (!AssetLib) return;

	const FGuid AssetId = AssetLib->GetSelectedPlacementAsset();
	if (!AssetId.IsValid()) return;

	RequestPlaceModel(AssetId, FTransform(Hit.Location));
}

void ATerraDyneEditController::SelectModelActor(ATerraDynePlacedModelActor* ModelActor)
{
	UTerraDyneToolWidget* ToolUI = GetToolWidget();
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

void ATerraDyneEditController::ClearModelSelection()
{
	UTerraDyneToolWidget* ToolUI = GetToolWidget();
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

void ATerraDyneEditController::RequestPlaceModel(const FGuid& AssetId, const FTransform& WorldTransform)
{
	if (!AssetId.IsValid()) return;

	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATerraDynePlacedModelActor* NewModel = World->SpawnActor<ATerraDynePlacedModelActor>(
				ATerraDynePlacedModelActor::StaticClass(),
				WorldTransform,
				SpawnParams);

			if (NewModel)
			{
				if (UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>())
				{
					if (const FTerraDyneImportedModelAsset* Asset = AssetLib->FindAsset(AssetId))
					{
						NewModel->SetModelAsset(*Asset);
					}
				}
				SelectModelActor(NewModel);
			}
		}
	}
	else
	{
		Server_PlaceModel(AssetId, WorldTransform);
	}
}

void ATerraDyneEditController::Server_PlaceModel_Implementation(FGuid AssetId, FTransform WorldTransform)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();
	RequestPlaceModel(AssetId, WorldTransform);
}

void ATerraDyneEditController::RequestMoveModel(ATerraDynePlacedModelActor* ModelActor, const FTransform& NewTransform)
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

void ATerraDyneEditController::Server_MoveModel_Implementation(ATerraDynePlacedModelActor* ModelActor, FTransform NewTransform)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();
	RequestMoveModel(ModelActor, NewTransform);
}

void ATerraDyneEditController::RequestDeleteModel(ATerraDynePlacedModelActor* ModelActor)
{
	if (!ModelActor) return;
	if (HasAuthority())
	{
		if (UTerraDyneToolWidget* ToolUI = GetToolWidget())
		{
			if (ToolUI->SelectedModelActor.Get() == ModelActor)
			{
				ClearModelSelection();
			}
		}
		ModelActor->Destroy();
	}
	else
	{
		Server_DeleteModel(ModelActor);
	}
}

void ATerraDyneEditController::Server_DeleteModel_Implementation(ATerraDynePlacedModelActor* ModelActor)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();
	RequestDeleteModel(ModelActor);
}

//-----------------------------------------------------------------------------
// OpenStreetMap ingestion
//-----------------------------------------------------------------------------

FTerraDyneOSMImportSummary ATerraDyneEditController::GetLastOSMImportSummary() const
{
	return LastOSMImportSummary;
}

void ATerraDyneEditController::RequestImportOSM(const FString& FilePath, bool bImportBuildings, bool bImportRoads)
{
	if (HasAuthority())
	{
		FTerraDyneOSMImportOptions Options;
		Options.bImportBuildings = bImportBuildings;
		Options.bImportRoads = bImportRoads;
		LastOSMImportSummary = ImportOSMInternal(FilePath, Options);
	}
	else
	{
		Server_ImportOSM(FilePath, bImportBuildings, bImportRoads);
	}
}

void ATerraDyneEditController::Server_ImportOSM_Implementation(const FString& FilePath, bool bImportBuildings, bool bImportRoads)
{
	TERRADYNE_REQUIRE_EDIT_AUTHORITY();
	RequestImportOSM(FilePath, bImportBuildings, bImportRoads);
}

FTerraDyneOSMImportSummary ATerraDyneEditController::ImportOSMInternal(const FString& FilePath, const FTerraDyneOSMImportOptions& Options)
{
	FTerraDyneOSMImportSummary Summary;

	UWorld* World = GetWorld();
	if (!World)
	{
		Summary.ErrorMessage = TEXT("No world to import into.");
		return Summary;
	}

	const FTerraDyneOSMData Data = FTerraDyneOSMParser::ParseFile(FilePath);
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

	int32 FeaturesCreated = 0;

	for (const FTerraDyneOSMWay& Way : Data.Ways)
	{
		const bool bIsBuilding = Options.bImportBuildings && FTerraDyneOSMParser::IsBuilding(Way);
		const bool bIsRoad = Options.bImportRoads && FTerraDyneOSMParser::IsDrivableHighway(Way);
		if (!bIsBuilding && !bIsRoad)
		{
			continue;
		}

		if (FeaturesCreated >= Options.MaxFeatures)
		{
			// Counted, not silently dropped -- the summary says how much was left behind.
			Summary.DroppedToLimit++;
			continue;
		}

		TArray<FVector> WorldPoints;
		if (!FTerraDyneOSMIngest::ResolveWayWorldPoints(Data, Way, LonLatToWorld, WorldPoints))
		{
			// A way reaching outside the extract's own bounds. Skip the whole way rather than
			// building a shape with a corner missing.
			bIsBuilding ? Summary.BuildingsSkipped++ : Summary.RoadsSkipped++;
			continue;
		}

		if (bIsBuilding)
		{
			FVector Origin = FVector::ZeroVector;
			const TArray<FVector> Local = FTerraDyneOSMIngest::BuildFootprintLocal(WorldPoints, Origin);
			if (Local.Num() < 3)
			{
				Summary.BuildingsSkipped++;
				continue;
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATerraDyneBuildingActor* NewBuilding = World->SpawnActor<ATerraDyneBuildingActor>(
				ATerraDyneBuildingActor::StaticClass(), FTransform(Origin), SpawnParams);
			if (!NewBuilding)
			{
				Summary.BuildingsSkipped++;
				continue;
			}

			NewBuilding->SetFloorCount(FTerraDyneOSMParser::ResolveBuildingLevels(Way, Options.DefaultBuildingLevels));
			NewBuilding->SetSplinePoints(Local);
			NewBuilding->SnapToPadHeight();

			Summary.BuildingsCreated++;
			FeaturesCreated++;
		}
		else
		{
			const TArray<FVector> Centreline = FTerraDyneOSMIngest::BuildRoadCentreline(WorldPoints);
			if (Centreline.Num() < 2)
			{
				Summary.RoadsSkipped++;
				continue;
			}

			// Virtual: does nothing unless the game module's subclass is the active controller.
			if (CreateRoadFromOSM(Centreline, FTerraDyneOSMParser::ResolveHighwayWidthMeters(Way)))
			{
				Summary.RoadsCreated++;
				FeaturesCreated++;
			}
			else
			{
				Summary.RoadsSkipped++;
			}
		}
	}

	Summary.bSuccess = true;
	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: OSM import of '%s' -- %s"), *FilePath, *Summary.ToDisplayString());
	return Summary;
}
