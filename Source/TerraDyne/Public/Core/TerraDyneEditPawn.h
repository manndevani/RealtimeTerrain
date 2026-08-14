// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "TerraDyneEditPawn.generated.h"

/**
 * Flying camera pawn for terrain editing.
 *
 * Exists because ADefaultPawn installs engine-defined DefaultPawn_Turn / DefaultPawn_LookUp axis
 * mappings from C++ (they never appear in DefaultInput.ini) and rotates the view on raw mouse
 * movement with no button held. That makes a left-drag sculpt swing the camera out from under the
 * stroke. This pawn disables those defaults and re-binds movement and look explicitly, so:
 *
 *   left  drag -> terrain edit only, camera pinned
 *   right drag -> orbit only
 *
 * Rotation lives here rather than in the player controller because ATerraDyneOrchestrator binds its
 * own sculpt input onto whatever controller is spawned. Owning the look input at the pawn keeps the
 * behaviour independent of which class ends up driving the edits.
 */
UCLASS()
class TERRADYNE_API ATerraDyneEditPawn : public ADefaultPawn
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
};
