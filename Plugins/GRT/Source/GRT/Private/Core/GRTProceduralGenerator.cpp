// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
#include "Core/GRTProceduralGenerator.h"

// Permutation table for Perlin noise
static const int32 PerlinPermutation[512] = {
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
	8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
	35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
	134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
	55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
	18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
	250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
	189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
	172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
	228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
	107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
	138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
	8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
	35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
	134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
	55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
	18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
	250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
	189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
	172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
	228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
	107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
	138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static FORCEINLINE float Fade(float T) { return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f); }
static FORCEINLINE float Lerp(float T, float A, float B) { return A + T * (B - A); }
static FORCEINLINE float Grad(int32 Hash, float X, float Y)
{
	const int32 H = Hash & 7;
	const float U = H < 4 ? X : Y;
	const float V = H < 4 ? Y : X;
	return ((H & 1) ? -U : U) + ((H & 2) ? -2.0f * V : 2.0f * V);
}

float FGRTProceduralGenerator::PerlinNoise(float X, float Y)
{
	const int32 X0 = FMath::FloorToInt(X) & 255;
	const int32 Y0 = FMath::FloorToInt(Y) & 255;
	const int32 X1 = (X0 + 1) & 255;
	const int32 Y1 = (Y0 + 1) & 255;

	const float Xf = X - FMath::FloorToFloat(X);
	const float Yf = Y - FMath::FloorToFloat(Y);

	const float U = Fade(Xf);
	const float V = Fade(Yf);

	const int32 A = PerlinPermutation[X0] + Y0;
	const int32 B = PerlinPermutation[X1] + Y0;

	return Lerp(V,
		Lerp(U, Grad(PerlinPermutation[A], Xf, Yf), Grad(PerlinPermutation[B], Xf - 1.0f, Yf)),
		Lerp(U, Grad(PerlinPermutation[A + 1], Xf, Yf - 1.0f), Grad(PerlinPermutation[B + 1], Xf - 1.0f, Yf - 1.0f)));
}

float FGRTProceduralGenerator::FractalNoise(float X, float Y, int32 Octaves, float Persistence)
{
	float Total = 0.0f;
	float Frequency = 1.0f;
	float Amplitude = 1.0f;
	float MaxValue = 0.0f;

	for (int32 I = 0; I < Octaves; ++I)
	{
		Total += PerlinNoise(X * Frequency, Y * Frequency) * Amplitude;
		MaxValue += Amplitude;
		Amplitude *= Persistence;
		Frequency *= 2.0f;
	}

	return MaxValue > 0.0f ? Total / MaxValue : 0.0f;
}

void FGRTProceduralGenerator::GenerateSandboxHeights(
	FIntPoint GridCoord,
	int32 Resolution,
	float WorldSize,
	int32 Seed,
	TArray<float>& OutBaseBuffer,
	TArray<float>& OutDetailBuffer)
{
	const int32 NumVerts = Resolution * Resolution;
	OutBaseBuffer.SetNumUninitialized(NumVerts);
	OutDetailBuffer.SetNumUninitialized(NumVerts);

	const FVector ChunkOrigin(GridCoord.X * WorldSize, GridCoord.Y * WorldSize, 0.0);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const double WorldX = ChunkOrigin.X + ((double(X) / (Resolution - 1)) - 0.5) * WorldSize;
			const double WorldY = ChunkOrigin.Y + ((double(Y) / (Resolution - 1)) - 0.5) * WorldSize;

			// Base Layer Noise (Macro Mountains & Hills)
			float Height = FractalNoise((float)(WorldX * 0.0005), (float)(WorldY * 0.0005), 4, 0.45f);
			Height = ProceduralFloor + Height * ProceduralRelief;
			OutBaseBuffer[Y * Resolution + X] = FMath::Clamp(Height, 0.0f, 1.0f);

			// Detail Layer Noise (Erosion / micro variations)
			float DetailHeight = FractalNoise((float)(WorldX * 0.0015), (float)(WorldY * 0.0015), 2) * 0.02f;
			OutDetailBuffer[Y * Resolution + X] = DetailHeight;
		}
	}
}
