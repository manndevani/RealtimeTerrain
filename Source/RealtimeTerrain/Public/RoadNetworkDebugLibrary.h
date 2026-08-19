// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RoadNetworkDebugLibrary.generated.h"

/**
 * Blueprint access to the intersection solver, for validating it against known cases.
 *
 * The solver itself is a plain namespace with no engine types in its interface, which keeps it
 * testable but also unreachable from script. This exists so the geometry can be checked numerically
 * — a symmetric four-way should give a square of a predictable size, a T should give five corners,
 * and so on — before it is wired to anything that draws.
 */
UCLASS()
class REALTIMETERRAIN_API URoadNetworkDebugLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Solves one intersection from straight arms described by heading and length.
	 *
	 * Arms are given as headings in degrees with a matching half-width each; every arm is treated
	 * as a straight run of ArmLength from the node. Returns the boundary in world XY and writes the
	 * per-arm trim distances back out in the same order the headings were given.
	 */
	UFUNCTION(BlueprintCallable, Category = "Road Network|Debug")
	static bool SolveStraightNode(
		const FVector2D& NodeLocation,
		const TArray<float>& HeadingsDegrees,
		const TArray<float>& HalfWidths,
		float ArmLength,
		TArray<FVector2D>& OutBoundary,
		TArray<float>& OutTrimDistances);
};
