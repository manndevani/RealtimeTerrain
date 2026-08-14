// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditPawn.h"
#include "Components/InputComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/NetSerialization.h"

ATerraDyneEditPawn::ATerraDyneEditPawn()
{
	// The whole point of this class: suppress ADefaultPawn's built-in bindings. They include
	// mouse-look on raw movement, which cannot be gated from a controller because it is installed
	// as an engine-defined axis mapping rather than a project one. Movement and look are re-bound
	// explicitly in SetupPlayerInputComponent using the TerraDyne* mappings from DefaultInput.ini.
	bAddDefaultMovementBindings = false;

	PrimaryActorTick.bCanEverTick = true;

	// Each machine drives its own camera, and position is pushed to the server explicitly in Tick.
	// Leaving movement replication on would make the server echo its stale copy back down to the
	// owning client and fight the local camera.
	SetReplicateMovement(false);

	// Terrain work happens at chunk scale (10000 uu chunks), so the stock 1200 uu/s crawls.
	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = 12000.0f;
		Movement->Acceleration = 40000.0f;
		Movement->Deceleration = 40000.0f;
	}
}

void ATerraDyneEditPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}

	// Look: bound unconditionally, but the handlers drop the input unless the right button is down.
	PlayerInputComponent->BindAction("TerraDyneRotate", IE_Pressed, this, &ATerraDyneEditPawn::OnOrbitStart);
	PlayerInputComponent->BindAction("TerraDyneRotate", IE_Released, this, &ATerraDyneEditPawn::OnOrbitStop);
	PlayerInputComponent->BindAxis("TerraDyneRotateYaw", this, &ATerraDyneEditPawn::OnLookYaw);
	PlayerInputComponent->BindAxis("TerraDyneRotatePitch", this, &ATerraDyneEditPawn::OnLookPitch);

	// Movement: re-added by hand since the defaults are off. ADefaultPawn already provides these
	// handlers; only the bindings were lost with bAddDefaultMovementBindings.
	PlayerInputComponent->BindAxis("TerraDyneMoveForward", this, &ATerraDyneEditPawn::MoveForward);
	PlayerInputComponent->BindAxis("TerraDyneMoveRight", this, &ATerraDyneEditPawn::MoveRight);
	PlayerInputComponent->BindAxis("TerraDyneMoveUp", this, &ATerraDyneEditPawn::MoveUp_World);
}

void ATerraDyneEditPawn::OnOrbitStart()
{
	bIsOrbiting = true;
}

void ATerraDyneEditPawn::OnOrbitStop()
{
	bIsOrbiting = false;
}

void ATerraDyneEditPawn::OnLookYaw(float Value)
{
	if (bIsOrbiting && Value != 0.0f)
	{
		AddControllerYawInput(Value * RotateSensitivity);
	}
}

void ATerraDyneEditPawn::OnLookPitch(float Value)
{
	if (bIsOrbiting && Value != 0.0f)
	{
		AddControllerPitchInput(Value * RotateSensitivity * (bInvertPitch ? -1.0f : 1.0f));
	}
}

void ATerraDyneEditPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only a client needs to report upward; the listen-server host already moves the authoritative
	// copy directly. Throttled because this exists to keep chunk streaming pointed at the right
	// place, not to reproduce smooth motion for other players.
	if (!IsLocallyControlled() || HasAuthority())
	{
		return;
	}

	ViewReportTimer -= DeltaTime;
	if (ViewReportTimer <= 0.0f)
	{
		ViewReportTimer = 0.2f;
		Server_ReportViewLocation(GetActorLocation());
	}
}

void ATerraDyneEditPawn::Server_ReportViewLocation_Implementation(FVector_NetQuantize NewLocation)
{
	// Teleport rather than sweep: this is a camera position, and sweeping it through terrain
	// collision on the server would snag it short of where the player actually is.
	SetActorLocation(NewLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
}
