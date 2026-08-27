// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditPawn.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "World/TerraDyneChunk.h"
#include "TerraDyneModule.h"
#include "CesiumGlobeAnchorComponent.h"
#include "CesiumGeoreference.h"
#include "CesiumFlyToComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "Engine/NetSerialization.h"
#include "Engine/World.h"

ATerraDyneEditPawn::ATerraDyneEditPawn()
	: AGlobeAwareDefaultPawn()
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

	// See the header comment on FlyToComponent: without this, FlyToCoordinates silently no-ops.
	FlyToComponent = CreateDefaultSubobject<UCesiumFlyToComponent>(TEXT("FlyToComponent"));
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

	// Mouse Wheel: adjust the flight speed multiplier instead of zooming/dollying the camera.
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ATerraDyneEditPawn::OnMouseWheelSpeedAdjust);

	// Frame entire map hotkeys (F or Home)
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ATerraDyneEditPawn::FocusOnMap);
	PlayerInputComponent->BindKey(EKeys::Home, IE_Pressed, this, &ATerraDyneEditPawn::FocusOnMap);
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

void ATerraDyneEditPawn::OnMouseWheelSpeedAdjust(float Value)
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

bool ATerraDyneEditPawn::IsShiftHeld() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
}

void ATerraDyneEditPawn::MoveForward(float Val)
{
	if (IsShiftHeld()) return;
	Super::MoveForward(Val);
}

void ATerraDyneEditPawn::MoveRight(float Val)
{
	if (IsShiftHeld()) return;
	Super::MoveRight(Val);
}

void ATerraDyneEditPawn::MoveUp_World(float Val)
{
	if (IsShiftHeld()) return;
	Super::MoveUp_World(Val);
}

void ATerraDyneEditPawn::FocusOnMap()
{
	UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Subsystem ? Subsystem->GetTerrainManager() : nullptr;
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

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: Focused camera on map center [%.0f, %.0f, %.0f], altitude %.0f"),
		TargetCenter.X, TargetCenter.Y, TargetCenter.Z, CamAltitude);
}

void ATerraDyneEditPawn::FocusOnLocation(FVector Location, float Distance)
{
	const float CamAltitude = Location.Z + Distance * 0.75f;
	SetActorLocation(FVector(Location.X, Location.Y - Distance * 0.75f, CamAltitude));
	SetActorRotation(FRotator(-40.0f, 90.0f, 0.0f));

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: Focused camera on location [%.0f, %.0f, %.0f], distance %.0f"),
		Location.X, Location.Y, Location.Z, Distance);
}

void ATerraDyneEditPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Dynamically adjust flight speed so the user can navigate across hundreds of km as easily as 100m.
	// Same real-WGS84-height reasoning as before -- raw Unreal Z stops meaning "altitude" far from
	// the georeference origin. FlightSpeedMultiplier (mouse wheel) scales this base up or down
	// without disturbing the altitude scaling itself.
	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		const double HeightMeters = GetCurrentLongitudeLatitudeHeight().Z;
		const float Altitude = FMath::Max(100.0f, FMath::Abs((float)(HeightMeters * 100.0)));
		Movement->MaxSpeed = FMath::Clamp(Altitude * 2.5f, 12000.0f, 50000000.0f) * FlightSpeedMultiplier;
		Movement->Acceleration = Movement->MaxSpeed * 3.0f;
		Movement->Deceleration = Movement->MaxSpeed * 3.0f;
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

void ATerraDyneEditPawn::Server_ReportViewLocation_Implementation(FVector_NetQuantize NewLocation)
{
	// Teleport rather than sweep: this is a camera position, and sweeping it through terrain
	// collision on the server would snag it short of where the player actually is.
	SetActorLocation(NewLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
}

FVector ATerraDyneEditPawn::GetCurrentLongitudeLatitudeHeight() const
{
	if (GlobeAnchor)
	{
		return GlobeAnchor->GetLongitudeLatitudeHeight();
	}
	return FVector::ZeroVector;
}

void ATerraDyneEditPawn::FlyToCoordinates(double Longitude, double Latitude, double Height, double Yaw, double Pitch)
{
	FlyToLocationLongitudeLatitudeHeight(
		FVector(Longitude, Latitude, Height),
		Yaw,
		Pitch,
		/*CanInterruptByMoving=*/true
	);
}
