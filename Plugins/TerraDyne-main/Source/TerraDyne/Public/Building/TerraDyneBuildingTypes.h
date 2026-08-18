// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "TerraDyneBuildingTypes.generated.h"

/**
 * Serializable state of one spline building.
 *
 * SplinePoints are stored LOCAL to Origin, deliberately. Moving a building is then a single
 * change to Origin — no per-point fixup — and the terrain pad recompute only has to re-offset
 * one transform. Storing them in world space would mean rewriting every point on every drag.
 */
USTRUCT(BlueprintType)
struct FTerraDyneBuildingData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	FGuid BuildingId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Footprint points, local to Origin. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	TArray<FVector> SplinePoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	bool bClosedLoop = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	int32 FloorCount = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	FSoftObjectPath KitAsset;

	/** World Z the footprint flattens to. Phase 2. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	float PadTargetZ = 0.0f;

	/** Blend skirt width in world units, from the footprint edge outwards. Phase 2. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	float PadFalloff = 400.0f;

	/** Which footprint segment gets the door panel on the ground floor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	int32 DoorSegmentIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Building")
	int32 RandomSeed = 0;
};
