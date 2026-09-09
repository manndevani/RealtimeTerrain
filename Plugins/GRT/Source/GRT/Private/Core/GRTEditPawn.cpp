// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/GRTEditPawn.h"
#include "Core/GRTManager.h"
#include "Core/GRTSubsystem.h"
#include "World/GRTChunk.h"
#include "GRTModule.h"
#include "CesiumGlobeAnchorComponent.h"
#include "CesiumGeoreference.h"
#include "CesiumFlyToComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "Engine/NetSerialization.h"
#include "Engine/World.h"

AGRTEditPawn::AGRTEditPawn()
	: AGlobeAwareDefaultPawn()
{
	// The whole point of this class: suppress ADefaultPawn's built-in bindings. They include
	// mouse-look on raw movement, which cannot be gated from a controller because it is installed
	// as an engine-defined axis mapping rather than a project one. Movement and look are re-bound
	// explicitly in SetupPlayerInputComponent using the GRT* mappings from DefaultInput.ini.
	bAddDefaultMovementBindings = false;

	PrimaryActorTick.bCanEverTick = true;

	// Each machine drives its own camera, and position is pushed to the server explicitly in Tick.
	// Leaving movement replication on would make the server echo its stale copy back down to the
	// owning client and fight the local camera.
	SetReplicateMovement(false);

	// Smooth base editing speed (2000 uu/s = 20 m/s) with snappy acceleration and responsive stopping
	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = 2000.0f;
		Movement->Acceleration = 8000.0f;
		Movement->Deceleration = 16000.0f;
	}

	// See the header comment on FlyToComponent: without this, FlyToCoordinates silently no-ops.
	FlyToComponent = CreateDefaultSubobject<UCesiumFlyToComponent>(TEXT("FlyToComponent"));
}

void AGRTEditPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}

	// Look: bound unconditionally, but the handlers drop the input unless the right button is down.
	PlayerInputComponent->BindAction("GRTRotate", IE_Pressed, this, &AGRTEditPawn::OnOrbitStart);
	PlayerInputComponent->BindAction("GRTRotate", IE_Released, this, &AGRTEditPawn::OnOrbitStop);
	PlayerInputComponent->BindAxis("GRTRotateYaw", this, &AGRTEditPawn::OnLookYaw);
	PlayerInputComponent->BindAxis("GRTRotatePitch", this, &AGRTEditPawn::OnLookPitch);

	// Movement: re-added by hand since the defaults are off. ADefaultPawn already provides these
	// handlers; only the bindings were lost with bAddDefaultMovementBindings.
	PlayerInputComponent->BindAxis("GRTMoveForward", this, &AGRTEditPawn::MoveForward);
	PlayerInputComponent->BindAxis("GRTMoveRight", this, &AGRTEditPawn::MoveRight);
	PlayerInputComponent->BindAxis("GRTMoveUp", this, &AGRTEditPawn::MoveUp_World);

	// Mouse Wheel: adjust the flight speed multiplier instead of zooming/dollying the camera.
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AGRTEditPawn::OnMouseWheelSpeedAdjust);

	// Frame entire map hotkeys (F or Home)
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AGRTEditPawn::FocusOnMap);
	PlayerInputComponent->BindKey(EKeys::Home, IE_Pressed, this, &AGRTEditPawn::FocusOnMap);
}

void AGRTEditPawn::OnOrbitStart()
{
	bIsOrbiting = true;
}

void AGRTEditPawn::OnOrbitStop()
{
	bIsOrbiting = false;
}

void AGRTEditPawn::OnLookYaw(float Value)
{
	if (bIsOrbiting && Value != 0.0f)
	{
		AddControllerYawInput(Value * RotateSensitivity);
	}
}

void AGRTEditPawn::OnLookPitch(float Value)
{
	if (bIsOrbiting && Value != 0.0f)
	{
		AddControllerPitchInput(Value * RotateSensitivity * (bInvertPitch ? -1.0f : 1.0f));
	}
}

void AGRTEditPawn::OnMouseWheelSpeedAdjust(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	// Multiplicative step, not additive: the automatic altitude-based base speed (see Tick) already
	// spans several orders of magnitude between ground level and orbit, so a fixed additive step
	// would be imperceptible zoomed out and wildly coarse zoomed in. A proportional step feels the
	// same regardless of where the multiplier currently sits.
	constexpr float StepFactor = 1.15f;
	FlightSpeedMultiplier = FMath::Clamp(
		FlightSpeedMultiplier * FMath::Pow(StepFactor, Value), 0.1f, 10.0f);
}

bool AGRTEditPawn::IsShiftHeld() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
}

void AGRTEditPawn::MoveForward(float Val)
{
	if (IsShiftHeld()) return;
	Super::MoveForward(Val);
}

void AGRTEditPawn::MoveRight(float Val)
{
	if (IsShiftHeld()) return;
	Super::MoveRight(Val);
}

void AGRTEditPawn::MoveUp_World(float Val)
{
	if (IsShiftHeld()) return;
	Super::MoveUp_World(Val);
}

void AGRTEditPawn::FocusOnMap()
{
	UGRTSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UGRTSubsystem>() : nullptr;
	AGRTManager* Manager = Subsystem ? Subsystem->GetTerrainManager() : nullptr;
	if (!Manager)
	{
		return;
	}

	FVector TargetCenter = FVector::ZeroVector;
	float TargetRadius = 50000.0f;

	if (Manager->GeoTIFFImportState.bWasImportedFromGeoTIFF)
	{
		const FVector2D MinXY = Manager->GeoTIFFImportState.WorldFootprintMin;
		const FVector2D MaxXY = Manager->GeoTIFFImportState.WorldFootprintMax;
		const FVector2D Center2D = (MinXY + MaxXY) * 0.5;
		const double ExtentX = FMath::Abs(MaxXY.X - MinXY.X);
		const double ExtentY = FMath::Abs(MaxXY.Y - MinXY.Y);
		TargetRadius = (float)FMath::Max(ExtentX, ExtentY) * 0.65f;
		TargetCenter = FVector((double)Center2D.X, (double)Center2D.Y, (double)Manager->GetActorLocation().Z);
	}
	else
	{
		FBox TotalBounds(ForceInit);
		for (const auto& Pair : Manager->GetActiveChunkMap())
		{
			if (Pair.Value)
			{
				TotalBounds += Pair.Value->GetComponentsBoundingBox(true);
			}
		}
		if (TotalBounds.IsValid)
		{
			TargetCenter = TotalBounds.GetCenter();
			TargetRadius = TotalBounds.GetExtent().GetMax() * 1.5f;
		}
	}

	TargetRadius = FMath::Max(TargetRadius, 30000.0f);
	const float CamAltitude = TargetCenter.Z + TargetRadius * 1.1f;
	SetActorLocation(FVector(TargetCenter.X, TargetCenter.Y - (TargetRadius * 0.15f), CamAltitude));
	SetActorRotation(FRotator(-80.0f, 90.0f, 0.0f));

	UE_LOG(LogGRT, Log, TEXT("GRT: Focused camera on map center [%.0f, %.0f, %.0f], altitude %.0f"),
		TargetCenter.X, TargetCenter.Y, TargetCenter.Z, CamAltitude);
}

void AGRTEditPawn::FocusOnLocation(FVector Location, float Distance)
{
	const float CamAltitude = Location.Z + Distance * 0.75f;
	SetActorLocation(FVector(Location.X, Location.Y - Distance * 0.75f, CamAltitude));
	SetActorRotation(FRotator(-40.0f, 90.0f, 0.0f));

	UE_LOG(LogGRT, Log, TEXT("GRT: Focused camera on location [%.0f, %.0f, %.0f], distance %.0f"),
		Location.X, Location.Y, Location.Z, Distance);
}

void AGRTEditPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Dynamically adjust flight speed so the user can navigate across hundreds of km as easily as 10m.
	// Near ground level (0 - 50m altitude), speed remains comfortable and precise (~2000 uu/s).
	// Higher altitudes scale progressively so flying across kilometers or whole continents remains responsive.
	// FlightSpeedMultiplier (mouse wheel) scales this base up or down for fine control.
	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		const double HeightMeters = GetCurrentLongitudeLatitudeHeight().Z;
		const float PositiveHeight = (float)FMath::Max(0.0, HeightMeters);
		const float BaseSpeed = FMath::Clamp(2000.0f + PositiveHeight * 10.0f, 1000.0f, 50000000.0f);
		Movement->MaxSpeed = BaseSpeed * FlightSpeedMultiplier;
		Movement->Acceleration = Movement->MaxSpeed * 4.0f;
		Movement->Deceleration = Movement->MaxSpeed * 8.0f;
	}

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

void AGRTEditPawn::Server_ReportViewLocation_Implementation(FVector_NetQuantize NewLocation)
{
	// Teleport rather than sweep: this is a camera position, and sweeping it through terrain
	// collision on the server would snag it short of where the player actually is.
	SetActorLocation(NewLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
}

FVector AGRTEditPawn::GetCurrentLongitudeLatitudeHeight() const
{
	if (GlobeAnchor)
	{
		return GlobeAnchor->GetLongitudeLatitudeHeight();
	}
	return FVector::ZeroVector;
}

void AGRTEditPawn::FlyToCoordinates(double Longitude, double Latitude, double Height, double Yaw, double Pitch)
{
	FlyToLocationLongitudeLatitudeHeight(
		FVector(Longitude, Latitude, Height),
		Yaw,
		Pitch,
		/*CanInterruptByMoving=*/true
	);
}
