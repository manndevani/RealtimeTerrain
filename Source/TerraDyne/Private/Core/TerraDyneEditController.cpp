// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "World/TerraDyneChunk.h"
#include "Engine/World.h"
#include "UI/TerraDyneToolWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

ATerraDyneEditController::ATerraDyneEditController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;
	UIClass = UTerraDyneToolWidget::StaticClass();
}

bool ATerraDyneEditController::IsInstructor() const
{
	// On a listen server the host's controller is both local and authoritative; a client's is local
	// but not authoritative, and the server's copy of a client is authoritative but not local.
	return IsLocalController() && HasAuthority();
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

	// Tool panel is per-machine UI, so only the player actually sitting at this client builds one.
	if (IsInstructor() && UIClass)
	{
		ActiveUI = CreateWidget<UUserWidget>(this, UIClass);
		if (ActiveUI)
		{
			ActiveUI->AddToViewport(100);
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

void ATerraDyneEditController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ATerraDyneEditController::OnLeftClickStart);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ATerraDyneEditController::OnLeftClickStop);
	InputComponent->BindAction("TerraDyneUndo", IE_Pressed, this, &ATerraDyneEditController::OnUndoPressed);
	InputComponent->BindAction("TerraDyneRedo", IE_Pressed, this, &ATerraDyneEditController::OnRedoPressed);
}

void ATerraDyneEditController::OnLeftClickStart()
{
	bIsClicking = true;
	bFlattenHeightLocked = false;
}

void ATerraDyneEditController::OnLeftClickStop()
{
	bIsClicking = false;
	bFlattenHeightLocked = false;
	if (bStrokeBegun)
	{
		bStrokeBegun = false;
		RequestCommitStroke();
	}
}

void ATerraDyneEditController::OnUndoPressed() { RequestUndo(); }
void ATerraDyneEditController::OnRedoPressed() { RequestRedo(); }

void ATerraDyneEditController::PerformToolAction()
{
	FVector WorldLoc, WorldDir;
	if (!DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return;
	}

	FHitResult Hit;
	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = true;
	if (APawn* MyPawn = GetPawn())
	{
		TraceParams.AddIgnoredActor(MyPawn);
	}

	if (!GetWorld()->LineTraceSingleByChannel(Hit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_WorldStatic, TraceParams))
	{
		return;
	}

	ATerraDyneManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	float Radius = 2000.0f;
	float Strength = 1000.0f;
	ETerraDyneToolMode Mode = ETerraDyneToolMode::SculptRaise;
	if (UTerraDyneToolWidget* ToolUI = Cast<UTerraDyneToolWidget>(ActiveUI))
	{
		Radius = ToolUI->BrushRadius;
		Strength = ToolUI->BrushStrength * 2500.0f;
		Mode = ToolUI->CurrentTool;
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

	if (bIsClicking && IsInstructor())
	{
		PerformToolAction();
	}

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
