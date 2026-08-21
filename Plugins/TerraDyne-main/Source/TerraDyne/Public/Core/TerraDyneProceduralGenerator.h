// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneWorldTypes.h"

/**
 * Dedicated procedural generator for Sandbox mode.
 * Encapsulates noise algorithms, procedural height bands, and detail layer synthesis.
 */
class TERRADYNE_API FTerraDyneProceduralGenerator
{
public:
	// Procedural relief bands (keeps procedural noise in a landscape-shaped band)
	static constexpr float ProceduralFloor  = 0.08f;
	static constexpr float ProceduralRelief = 0.32f;

	/** Generate Base and Detail height buffers for a chunk in Sandbox mode */
	static void GenerateSandboxHeights(
		FIntPoint GridCoord,
		int32 Resolution,
		float WorldSize,
		int32 Seed,
		TArray<float>& OutBaseBuffer,
		TArray<float>& OutDetailBuffer);

	/** Perlin noise function for coordinate (X, Y) */
	static float PerlinNoise(float X, float Y);

	/** Fractal Brownian Motion (fBm) noise with octaves and persistence */
	static float FractalNoise(float X, float Y, int32 Octaves = 4, float Persistence = 0.5f);
};
