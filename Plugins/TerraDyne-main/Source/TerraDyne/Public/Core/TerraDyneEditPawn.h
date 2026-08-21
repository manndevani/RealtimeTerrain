// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GlobeAwareDefaultPawn.h"
#include "TerraDyneEditPawn.generated.h"

/**
 * Flying camera pawn for terrain editing with full Cesium Globe awareness.
 *
 * Inherits from Cesium's AGlobeAwareDefaultPawn to provide:
 *   - Native UCesiumGlobeAnchorComponent integration for precise WGS84 and ECEF georeferencing
 *   - Correct East-South-Up (ESU) horizon leveling and Ellipsoid normal-aligned vertical movement
 *   - Seamless FlyTo navigation across planet-scale and regional GIS DEM coordinates
 *
 * While retaining TerraDyne's editing controls:
 *   - Left-drag: terrain sculpting / road editing only (camera pinned)
 *   - Right-drag: orbit / look
 *   - Mouse wheel: smooth altitude-scaled zoom
 *   - F / Home key: frame entire GeoTIFF or sandbox map
 *   - Dynamic altitude speed scaling for effortless navigation from 100m to 500km
 */
UCLASS()
class TERRADYNE_API ATerraDyneEditPawn : public AGlobeAwareDefaultPawn
{
	GENERATED_BODY()

public:
	ATerraDyneEditPawn();

	/** Degrees of rotation per unit of mouse movement while right-dragging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Input")
	float RotateSensitivity = 1.0f;

	/** Invert vertical look while right-dragging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Input")
	bool bInvertPitch = false;

	/** True while the right mouse button is held. Look input is ignored unless this is set. */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Input")
	bool IsOrbiting() const { return bIsOrbiting; }

	/** Frame the entire active terrain (GeoTIFF footprint or sandbox chunks) in a top-down view */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Camera")
	void FocusOnMap();

	/** Get current WGS84 coordinates (Longitude, Latitude, Height in meters) from the Globe Anchor */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|GIS")
	FVector GetCurrentLongitudeLatitudeHeight() const;

	/** Fly smoothly to real-world WGS84 coordinates */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|GIS")
	void FlyToCoordinates(double Longitude, double Latitude, double Height, double Yaw = 0.0, double Pitch = -45.0);

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;

private:
	bool bIsOrbiting = false;

	/**
	 * Push this pawn's position up to the server periodically.
	 *
	 * UFloatingPawnMovement, unlike UCharacterMovementComponent, has no networked movement — a
	 * client flies around locally while the server's copy of that pawn stays wherever it spawned.
	 * ATerraDyneManager streams chunks around every player's server-side position, so without this
	 * a viewer who moves away from the origin is streamed no terrain at all.
	 */
	UFUNCTION(Server, Unreliable)
	void Server_ReportViewLocation(FVector_NetQuantize NewLocation);

	float ViewReportTimer = 0.0f;

	void OnOrbitStart();
	void OnOrbitStop();
	void OnLookYaw(float Value);
	void OnLookPitch(float Value);
	void OnMouseWheelZoom(float Value);
};
