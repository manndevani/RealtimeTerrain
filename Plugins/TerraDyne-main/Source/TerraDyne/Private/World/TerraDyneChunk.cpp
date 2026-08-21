// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneChunk.h"
#include "Engine/World.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSaveGame.h"
#include "Core/TerraDyneSubsystem.h"
#include "Settings/TerraDyneSettings.h"
#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "RenderingThread.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Core/TerraDyneProceduralGenerator.h"
#include "IO/TerraDyneDEMMeshBuilder.h"

// Unit gradient for an integer lattice corner, from a deterministic hash of its coordinates.
// Depends only on absolute lattice position, so neighbouring chunks derive identical gradients for
// the corners they share and the field is continuous across every chunk boundary by construction.
static FORCEINLINE void LatticeGradient(int32 X, int32 Y, float& OutGX, float& OutGY)
{
	uint32 H = (uint32)X * 374761393u + (uint32)Y * 668265263u;
	H = (H ^ (H >> 13)) * 1274126177u;
	H ^= (H >> 16);

	// 8 evenly spaced gradients — enough directions to break up the axis alignment, and no trig
	// call per corner.
	static const float GradX[8] = { 1.f, -1.f, 0.f,  0.f,  0.70710678f, -0.70710678f,  0.70710678f, -0.70710678f };
	static const float GradY[8] = { 0.f,  0.f, 1.f, -1.f,  0.70710678f,  0.70710678f, -0.70710678f, -0.70710678f };

	const uint32 Index = H & 7u;
	OutGX = GradX[Index];
	OutGY = GradY[Index];
}

// Perlin-style 2D gradient noise, output in [-1, 1].
static float GradientNoise2D(float X, float Y)
{
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const float FX = X - (float)X0;
	const float FY = Y - (float)Y0;

	// Quintic fade (6t^5 - 15t^4 + 10t^3): C2-continuous, unlike a plain smoothstep, so summed
	// octaves don't show creases along the lattice lines.
	const float UX = FX * FX * FX * (FX * (FX * 6.0f - 15.0f) + 10.0f);
	const float UY = FY * FY * FY * (FY * (FY * 6.0f - 15.0f) + 10.0f);

	auto CornerDot = [](int32 IX, int32 IY, float DX, float DY)
	{
		float GX, GY;
		LatticeGradient(IX, IY, GX, GY);
		return GX * DX + GY * DY;
	};

	const float N00 = CornerDot(X0,     Y0,     FX,        FY);
	const float N10 = CornerDot(X0 + 1, Y0,     FX - 1.0f, FY);
	const float N01 = CornerDot(X0,     Y0 + 1, FX,        FY - 1.0f);
	const float N11 = CornerDot(X0 + 1, Y0 + 1, FX - 1.0f, FY - 1.0f);

	// Gradient noise peaks near +/-0.7071; scale so the result spans roughly [-1, 1].
	return FMath::Lerp(FMath::Lerp(N00, N10, UX), FMath::Lerp(N01, N11, UX), UY) * 1.41421356f;
}

// Fractal Brownian Motion over GradientNoise2D, normalized to [0, 1].
//
// This replaced a separable Sin(X) + Cos(Y) field. That field was cheap but its two terms varied
// independently along each axis, which carved the terrain into axis-aligned flutes rather than
// anything landform-shaped. It also summed to [-0.5, 1.5] before the caller's 0..1 clamp, so the
// top and bottom third of every peak and valley was clipped flat into mesas.
static float FractalNoise(float X, float Y, int32 Octaves, float Persistence = 0.5f, float Lacunarity = 2.0f)
{
	float Total = 0.0f;
	float Frequency = 1.0f;
	float Amplitude = 1.0f;
	float MaxValue = 0.0f;

	for (int32 i = 0; i < Octaves; i++)
	{
		Total += GradientNoise2D(X * Frequency, Y * Frequency) * Amplitude;
		MaxValue += Amplitude;

		Amplitude *= Persistence;
		Frequency *= Lacunarity;
	}

	// Total/MaxValue is in [-1, 1]; remap to [0, 1] so the caller's clamp only ever trims the
	// extremes instead of flattening whole regions.
	return (MaxValue > 0.0f) ? (Total / MaxValue) * 0.5f + 0.5f : 0.5f;
}

static void ApplyTerrainMaterialParameters(
	UMaterialInstanceDynamic* MID,
	UTexture* HeightTexture,
	float WorldSize,
	float ZScale,
	int32 Resolution)
{
	if (!MID)
	{
		return;
	}

	if (HeightTexture)
	{
		MID->SetTextureParameterValue(TEXT("HeightMap"), HeightTexture);
	}

	// WeightMap has to be bound to something neutral even though the layer split now comes from
	// vertex colors (see ApplyLayerWeightsToMesh). Every shipped terrain material still declares
	// the parameter, and both M_TerraDyne_Master and the MI_TerraDyne_* instances default it to
	// /Engine/EngineResources/DefaultTexture — which is the engine CHECKERBOARD. Leaving it unbound
	// therefore does not fall back to "no weights", it multiplies the ground blend by a checker and
	// tiles the whole landscape with grey squares.
	//
	// White is the identity here: verified in PIE, binding it restores the correct rock/dirt/grass/
	// snow blend, while the checker returns the moment it is removed.
	static UTexture* NeutralWeightTexture = LoadObject<UTexture>(nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (NeutralWeightTexture)
	{
		MID->SetTextureParameterValue(TEXT("WeightMap"), NeutralWeightTexture);
	}

	MID->SetScalarParameterValue(TEXT("WorldSize"), WorldSize);
	MID->SetScalarParameterValue(TEXT("ZScale"), ZScale);
	MID->SetScalarParameterValue(
		TEXT("TexelStep"),
		(Resolution > 1) ? (1.0f / static_cast<float>(Resolution - 1)) : 0.0f);
}

// bSRGB must mirror the SamplerType the master material declares for the matching texture
// parameter. UMaterialInstanceDynamic::SetTextureParameterValue rejects a texture whose sRGB flag
// disagrees with the compiled sampler and silently leaves the parameter on its default texture
// (/Engine/EngineResources/DefaultTexture, a checkerboard) — so a mismatch here shows up as a
// checkerboard blended across the terrain rather than as any kind of error.
//
// HeightMap is SAMPLERTYPE_LinearColor (height is data, not color) and so is created linear.
//
// The height texture is additionally FP16 rather than 8-bit. The material derives its surface normal
// by finite-differencing this texture, and at ZScale 8000 over a WorldSize/Resolution texel spacing
// (~79 uu at the defaults) a single 8-bit level is ~31 uu of rise — roughly 20 degrees of apparent
// slope. An 8-bit heightmap therefore terraces the derived normal into quantization bands rather
// than terrain shape, so the slope mask becomes high-frequency noise instead of following the
// terrain. FP16 also matches HeightRT's format, so both height paths feed the material identically.
static UTexture2D* CreateChunkDataTexture(int32 Resolution, EPixelFormat Format, bool bSRGB, const TCHAR* DebugName)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Resolution, Resolution, Format, DebugName);
	if (Texture)
	{
		Texture->NeverStream = true;
		Texture->Filter = TF_Bilinear;
		Texture->SRGB = bSRGB;
		Texture->UpdateResource();
	}
	return Texture;
}

// Terrain layer thresholds, in the same normalized 0-1 space as HeightBuffer, and in "1 - Normal.Z"
// for slope (0 = flat, 0.29 ~ 45 degrees, 0.5 = 60 degrees, 0.74 ~ 75 degrees).
// Deliberately plain constants rather than UPROPERTYs: adding reflected properties forces a full
// rebuild and editor restart, and these want to be iterated on with Live Coding.
namespace TerraDyneLayers
{
	// ZScale is deliberately generous (8000) to leave sculpting headroom, but the *procedural* pass
	// should only occupy part of it. Letting the base noise span the full 0-1 put 8000 uu of relief
	// inside a 10000 uu chunk, which measured out at an average surface slope of 61 degrees — every
	// vertex read as a cliff, so the slope mask saturated to rock and no amount of material tuning
	// could produce a layer split. These keep procedural terrain in a landscape-shaped band and
	// leave the rest of ZScale free for the brush to raise into.
	static constexpr float ProceduralFloor  = 0.08f;
	static constexpr float ProceduralRelief = 0.32f;

	// Slope thresholds in "1 - Normal.Z" (0.13 ~ 30 degrees, 0.29 ~ 45, 0.5 = 60).
	static constexpr float RockSlopeStart = 0.22f; // rock starts appearing around 38 degrees
	static constexpr float RockSlopeFull  = 0.50f; // and owns the surface by 60 degrees

	// Snow thresholds as a fraction of the procedural relief band, so they stay meaningful
	// independently of ZScale or how much headroom is reserved above the terrain.
	static constexpr float SnowHeightStart = 0.62f;
	static constexpr float SnowHeightFull  = 0.90f;
}

// Layer banding has to follow broad landform rather than per-vertex relief. Keyed off the raw
// height, every small bump crosses the thresholds on its own, so snow caps each individual lump and
// the result reads as salt-and-pepper speckle instead of elevation bands. A separable box blur over
// a handful of vertices keeps the bands reading as terrain. Geometry still uses the unsmoothed
// HeightBuffer — this only feeds the layer split.
static void BuildBandingHeightField(const TArray<float>& Source, int32 Resolution, int32 Radius, TArray<float>& Out)
{
	const int32 Count = Resolution * Resolution;
	Out.SetNumUninitialized(Count);
	if (Source.Num() != Count || Resolution < 2 || Radius < 1)
	{
		for (int32 i = 0; i < Count; i++) { Out[i] = Source.IsValidIndex(i) ? Source[i] : 0.f; }
		return;
	}

	// Running-sum box blur: each step adds the pixel entering the window and drops the one leaving,
	// so cost is independent of Radius. The naive form sampled (2*Radius+1) values per pixel per
	// axis — at Resolution 128 and Radius 6 that was ~426k samples every time this ran, and it runs
	// per chunk per frame during a drag.
	const float InvWindow = 1.0f / (float)(2 * Radius + 1);

	static thread_local TArray<float> Horizontal;
	Horizontal.SetNumUninitialized(Count, EAllowShrinking::No);

	for (int32 Y = 0; Y < Resolution; Y++)
	{
		const int32 Row = Y * Resolution;

		// Seed the window at X = 0, with out-of-range taps clamped to the edge pixel.
		float Sum = Source[Row] * (float)(Radius + 1);
		for (int32 K = 1; K <= Radius; K++)
		{
			Sum += Source[Row + FMath::Min(K, Resolution - 1)];
		}

		for (int32 X = 0; X < Resolution; X++)
		{
			Horizontal[Row + X] = Sum * InvWindow;
			const int32 Entering = FMath::Min(X + Radius + 1, Resolution - 1);
			const int32 Leaving = FMath::Max(X - Radius, 0);
			Sum += Source[Row + Entering] - Source[Row + Leaving];
		}
	}

	for (int32 X = 0; X < Resolution; X++)
	{
		float Sum = Horizontal[X] * (float)(Radius + 1);
		for (int32 K = 1; K <= Radius; K++)
		{
			Sum += Horizontal[FMath::Min(K, Resolution - 1) * Resolution + X];
		}

		for (int32 Y = 0; Y < Resolution; Y++)
		{
			Out[Y * Resolution + X] = Sum * InvWindow;
			const int32 Entering = FMath::Min(Y + Radius + 1, Resolution - 1);
			const int32 Leaving = FMath::Max(Y - Radius, 0);
			Sum += Horizontal[Entering * Resolution + X] - Horizontal[Leaving * Resolution + X];
		}
	}
}

static FORCEINLINE float SmoothBand(float Edge0, float Edge1, float X)
{
	const float T = FMath::Clamp((X - Edge0) / FMath::Max(Edge1 - Edge0, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

// Resolve the Grass/Rock/Snow split for one vertex, packed for the vertex-color channels: RGB are
// the three weights and A is normalized height, which the material uses for elevation banding.
//
// This lives on the CPU because the CPU already holds the exact float height and the exact vertex
// normal. The material's previous approach re-derived a normal by finite-differencing the heightmap
// texture, which made the entire blend hostage to that texture's bit depth, sRGB flag and UV
// mapping — three independent ways for the layer split to silently collapse to a single layer.
static FVector4f ComputeLayerWeights(float NormalizedHeight, float NormalZ)
{
	// Re-normalize against the procedural relief band so both the snow gate here and the material's
	// elevation banding see a full 0-1 range, rather than the narrow slice of ZScale terrain
	// actually occupies.
	const float Height = FMath::Clamp(
		(NormalizedHeight - TerraDyneLayers::ProceduralFloor) / TerraDyneLayers::ProceduralRelief,
		0.0f, 1.0f);
	const float Slope = FMath::Clamp(1.0f - NormalZ, 0.0f, 1.0f);

	const float RockW = SmoothBand(TerraDyneLayers::RockSlopeStart, TerraDyneLayers::RockSlopeFull, Slope);
	// Snow settles on surfaces that are both high and not sheer, so it yields to rock on cliff faces.
	const float SnowW = SmoothBand(TerraDyneLayers::SnowHeightStart, TerraDyneLayers::SnowHeightFull, Height) * (1.0f - RockW);
	const float GrassW = FMath::Max(0.0f, 1.0f - RockW - SnowW);

	return FVector4f(GrassW, RockW, SnowW, Height);
}

// Write the layer split into the mesh's vertex colors. Shared by RebuildMesh and UpdateMeshHeights:
// sculpting goes through the latter, and if only that path updated positions the weights would stay
// frozen at whatever the last full rebuild produced, so edited terrain kept its old layers.
// Call after the vertex normals are up to date — the slope term reads them.
static void ApplyLayerWeightsToMesh(UE::Geometry::FDynamicMesh3& Mesh, const TArray<float>& HeightBuffer, int32 Resolution)
{
	const int32 Count = Resolution * Resolution;
	if (Count <= 0 || HeightBuffer.Num() != Count)
	{
		return;
	}

	TArray<float> BandingHeight;
	BuildBandingHeightField(HeightBuffer, Resolution, 6, BandingHeight);

	Mesh.Attributes()->EnablePrimaryColors();
	UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
	if (!ColorOverlay)
	{
		return;
	}

	// Vertices are appended in HeightBuffer order, so element index == vertex ID. On a re-edit the
	// elements already exist and only need new values; a fresh mesh needs them created and wired.
	const bool bReuseElements = (ColorOverlay->ElementCount() == Count);
	if (!bReuseElements)
	{
		ColorOverlay->ClearElements();
	}

	for (int32 VID = 0; VID < Count; VID++)
	{
		const FVector4f Weights = ComputeLayerWeights(BandingHeight[VID], (float)Mesh.GetVertexNormal(VID).Z);
		if (bReuseElements)
		{
			ColorOverlay->SetElement(VID, Weights);
		}
		else
		{
			ColorOverlay->AppendElement(Weights);
		}
	}

	if (!bReuseElements)
	{
		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			ColorOverlay->SetTriangle(TID, Mesh.GetTriangle(TID));
		}
	}
}

/**
 * Signed distance from a point to a closed polygon in 2D. Negative inside, positive outside.
 *
 * Magnitude is the distance to the nearest edge (not the nearest vertex), which is what makes the
 * blend skirt follow the footprint evenly instead of bulging at the corners. The sign comes from a
 * crossing-number test run in the same loop, so the whole thing is one pass over the edges.
 */
static float SignedDistanceToPolygon2D(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	const int32 Num = Polygon.Num();
	if (Num < 3)
	{
		return TNumericLimits<float>::Max();
	}

	float MinDistSq = TNumericLimits<float>::Max();
	bool bInside = false;

	for (int32 i = 0, j = Num - 1; i < Num; j = i++)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[j];

		// Distance to segment A->B, with the degenerate zero-length edge folded into the endpoint.
		const FVector2D Edge = B - A;
		const FVector2D ToPoint = Point - A;
		const float EdgeLenSq = Edge.SizeSquared();
		const float T = (EdgeLenSq > SMALL_NUMBER)
			? FMath::Clamp(FVector2D::DotProduct(ToPoint, Edge) / EdgeLenSq, 0.0f, 1.0f)
			: 0.0f;
		MinDistSq = FMath::Min(MinDistSq, (ToPoint - Edge * T).SizeSquared());

		if (((A.Y > Point.Y) != (B.Y > Point.Y)) &&
			(Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X))
		{
			bInside = !bInside;
		}
	}

	const float Dist = FMath::Sqrt(MinDistSq);
	return bInside ? -Dist : Dist;
}

static int32 ResolveSquareResolutionFromSampleCount(int32 SampleCount)
{
	if (SampleCount <= 0)
	{
		return 0;
	}

	const int32 DerivedResolution = FMath::RoundToInt(FMath::Sqrt(static_cast<double>(SampleCount)));
	return (DerivedResolution > 1 && DerivedResolution * DerivedResolution == SampleCount)
		? DerivedResolution
		: 0;
}

static void ApplyBrushCPUInternal(TArray<float>& HeightBuffer, int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength, float ZScale)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);
	
	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);
	
	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);
	
	// Convert world strength to normalized height space (0-1 range)
	// Strength is in world units (e.g., 5000), ZScale is max height (e.g., 3000)
	float StrengthNorm = Strength / ZScale;
	
	int32 ModifiedPixels = 0;
	
	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			
			if (DistSq <= Radius * Radius)
			{
				float Dist = FMath::Sqrt(DistSq);
				float Falloff = 1.0f - (Dist / Radius);
				Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);
				
				int32 Index = Y * Resolution + X;
				float Delta = StrengthNorm * Falloff;
				HeightBuffer[Index] = FMath::Clamp(HeightBuffer[Index] + Delta, -1.f, 2.f);
				ModifiedPixels++;
			}
		}
	}
	
	UE_LOG(LogTemp, Verbose, TEXT("CPU Brush: Modified %d pixels"), ModifiedPixels);
}

static void ApplySmoothCPUInternal(TArray<float>& Buffer, int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);

	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);

	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);

	// Snapshot prevents pixels from influencing each other mid-pass
	TArray<float> Snapshot = Buffer;

	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			if (DistSq > Radius * Radius) continue;

			float Dist = FMath::Sqrt(DistSq);
			float Falloff = 1.0f - (Dist / Radius);
			Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);

			// 8-neighbor average from snapshot
			float Sum = 0.f;
			int32 Count = 0;
			for (int32 DY = -1; DY <= 1; DY++)
			{
				for (int32 DX = -1; DX <= 1; DX++)
				{
					if (DX == 0 && DY == 0) continue;
					int32 NX = X + DX;
					int32 NY = Y + DY;
					if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution) continue;
					Sum += Snapshot[NY * Resolution + NX];
					Count++;
				}
			}
			float NeighborAvg = (Count > 0) ? (Sum / Count) : Snapshot[Y * Resolution + X];
			Buffer[Y * Resolution + X] = FMath::Lerp(Buffer[Y * Resolution + X], NeighborAvg, Strength * Falloff);
		}
	}
}

static void ApplyFlattenCPUInternal(TArray<float>& SculptBuffer, const TArray<float>& BaseBuffer, const TArray<float>& DetailBuffer,
	int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength, float FlattenHeightNorm)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);

	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);

	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);

	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			if (DistSq > Radius * Radius) continue;

			float Dist = FMath::Sqrt(DistSq);
			float Falloff = 1.0f - (Dist / Radius);
			Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);

			int32 Idx = Y * Resolution + X;
			// Target sculpt value: total height = FlattenHeightNorm => Sculpt = target - Base - Detail
			float TargetSculpt = FlattenHeightNorm - BaseBuffer[Idx] - DetailBuffer[Idx];
			SculptBuffer[Idx] = FMath::Lerp(SculptBuffer[Idx], TargetSculpt, Strength * Falloff);
		}
	}
}

ATerraDyneChunk::ATerraDyneChunk()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// Terrain is the world, not an object in it, so it must not be distance-culled from clients.
	// With the default cull radius (~15000 uu) a player looking down from the map's spawn height
	// sits outside every chunk's radius and receives none of them — the client sees empty space
	// while the server holds a full landscape. ATerraDyneManager already bounds how many chunks
	// exist at all via streaming, so a second distance filter on top only hides terrain that was
	// deliberately streamed in.
	bAlwaysRelevant = true;

	DynamicMeshComp = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComp"));
	SetRootComponent(DynamicMeshComp);

	DynamicMeshComp->SetCollisionProfileName(TEXT("BlockAll"));
	DynamicMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DynamicMeshComp->SetCollisionObjectType(ECC_WorldStatic);
	DynamicMeshComp->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	DynamicMeshComp->bEnableComplexCollision = true;
	DynamicMeshComp->bDeferCollisionUpdates = false;
	
	DynamicMeshComp->SetVisibility(true);
	DynamicMeshComp->SetHiddenInGame(false);
	DynamicMeshComp->SetCastShadow(true);

	// RebuildMesh bakes the terrain layer weights into vertex colors (see ComputeLayerWeights) for
	// the material's VertexColor node to read. The override mode stays None on purpose: VertexColors
	// is a debug display mode that paints the raw colors *instead of* evaluating the material, which
	// would render the terrain as its own weight mask. Having the color overlay is all the material
	// needs. NoTransform is required rather than cosmetic — these are blend weights, and a colour
	// space conversion would bend every one of them.
	DynamicMeshComp->SetColorOverrideMode(EDynamicMeshComponentColorOverrideMode::None);
	DynamicMeshComp->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::NoTransform);
	
	// Hard ceiling for world-space terrain height: HeightBuffer is always clamped to [0,1] after
	// Base+Sculpt+Detail, so max achievable height = ZScale exactly, no matter how much you Raise.
	// 3000 clipped sculpted peaks into flat plateaus once fully raised — 8000 gives real headroom
	// for dramatic mountains before hitting that ceiling.
	ZScale = 8000.0f;
	WorldSize = 10000.0f;
	Resolution = 128; // Increased from 64 for better visual quality
	bInitialized = false;
	bUseGPU = false;
	HeightRT = nullptr;
	HeightRT_Swap = nullptr;
	CollisionDebounceTimer = 0.0f;
	bCollisionDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;
	bTransferredFoliageDirty = false;
	bTransferredFoliageFollowsTerrain = false;
}

void ATerraDyneChunk::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATerraDyneChunk, ZScale);
	DOREPLIFETIME(ATerraDyneChunk, WorldSize);
	DOREPLIFETIME(ATerraDyneChunk, Resolution);
	DOREPLIFETIME(ATerraDyneChunk, GridCoordinate);
	DOREPLIFETIME(ATerraDyneChunk, ChunkSizeWorldUnits);
	DOREPLIFETIME(ATerraDyneChunk, ProceduralSeed);
	DOREPLIFETIME(ATerraDyneChunk, bIsAuthoredChunk);
	DOREPLIFETIME(ATerraDyneChunk, PrimaryBiomeTag);
}

void ATerraDyneChunk::BeginPlay()
{
	Super::BeginPlay();

	ATerraDyneManager* Manager = nullptr;
	if (UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
	{
		Manager = Subsystem->GetTerrainManager();
	}
	if (!Manager && GetWorld())
	{
		Manager = Cast<ATerraDyneManager>(UGameplayStatics::GetActorOfClass(GetWorld(), ATerraDyneManager::StaticClass()));
	}

	const bool bIsDEMMode = Manager && (Manager->WorldOriginKind == ETerraDyneChunkOrigin::ImportedGeoTIFF);

	if (!bInitialized && !bIsDEMMode && !bIsAuthoredChunk)
	{
		Initialize(Resolution, WorldSize);
	}

	if (Manager)
	{
		Manager->RegisterChunk(this);
	}
}

void ATerraDyneChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTransferredActorFoliageActors();
	ClearTransferredFoliageComponents();

	if (UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
	{
		if (ATerraDyneManager* Manager = Subsystem->GetTerrainManager())
		{
			Manager->UnregisterChunk(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ATerraDyneChunk::InitializeChunk(FIntPoint Coord, float Size, int32 InRes, UTexture2D* SourceHeight)
{
	GridCoordinate = Coord;
	ChunkSizeWorldUnits = Size;
	WorldSize = Size;

	Initialize(FMath::Clamp(InRes, 32, 256), Size);

#if WITH_EDITORONLY_DATA
	// Seed BaseBuffer from a baked height texture if provided
	if (SourceHeight && SourceHeight->GetPlatformData() && SourceHeight->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = SourceHeight->GetPlatformData()->Mips[0];
		const void* RawData = Mip.BulkData.LockReadOnly();
		if (RawData)
		{
			const uint8* Pixels = static_cast<const uint8*>(RawData);
			int32 NumPx = Resolution * Resolution;
			for (int32 i = 0; i < NumPx && i < BaseBuffer.Num(); i++)
			{
				// Assumes PF_G8 (single greyscale byte per pixel)
				BaseBuffer[i] = Pixels[i] / 255.f;
			}
			RecomputeHeightFromLayers();
			Mip.BulkData.Unlock();
			RebuildMesh();
			UpdateCollision();
		}
	}

#endif
}

void ATerraDyneChunk::Initialize(int32 InResolution, float InSize)
{
	if (bInitialized) return;
	
	Resolution = InResolution;
	WorldSize = InSize;

	PopulateProcedural();
}

void ATerraDyneChunk::PopulateProcedural()
{
	UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d]: Initializing Procedural Res=%d Size=%.0f"),
		GridCoordinate.X, GridCoordinate.Y, Resolution, WorldSize);

	int32 NumVerts = Resolution * Resolution;
	BaseBuffer.SetNumZeroed(NumVerts);
	SculptBuffer.SetNumZeroed(NumVerts);
	DetailBuffer.SetNumZeroed(NumVerts);
	HeightBuffer.SetNumZeroed(NumVerts);
	PadWeightBuffer.SetNumZeroed(NumVerts);
	PadTargetBuffer.SetNumZeroed(NumVerts);

	if (!CPUHeightTexture)
	{
		CPUHeightTexture = CreateChunkDataTexture(Resolution, PF_FloatRGBA, /*bSRGB=*/false, TEXT("CPUHeightTexture"));
	}

	FTerraDyneProceduralGenerator::GenerateSandboxHeights(
		GridCoordinate,
		Resolution,
		WorldSize,
		ProceduralSeed,
		BaseBuffer,
		DetailBuffer
	);

	RecomputeHeightFromLayers();
	UploadCPUHeightTexture();
	RebuildMesh();
	UpdateCollision();

	bInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d]: Ready (Procedural)"), GridCoordinate.X, GridCoordinate.Y);
}

void ATerraDyneChunk::PopulateFromDEM(TArray<float>&& InHeightBuffer, float InZScale, double InWorldZOffset, bool bGenerateCollision)
{
	const int32 NumVerts = Resolution * Resolution;
	if (InHeightBuffer.Num() != NumVerts)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: PopulateFromDEM sample mismatch (%d vs %d)"),
			GridCoordinate.X, GridCoordinate.Y, InHeightBuffer.Num(), NumVerts);
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	SetActorLocation(FVector(CurrentLocation.X, CurrentLocation.Y, InWorldZOffset));
	ZScale = InZScale;

	BaseBuffer = MoveTemp(InHeightBuffer);
	SculptBuffer.SetNumZeroed(NumVerts);
	DetailBuffer.SetNumZeroed(NumVerts);
	HeightBuffer.SetNumZeroed(NumVerts);
	PadWeightBuffer.SetNumZeroed(NumVerts);
	PadTargetBuffer.SetNumZeroed(NumVerts);

	RecomputeHeightFromLayers();

	// High-speed DEM mesh building (bypasses procedural noise & texture uploads)
	FTerraDyneDEMMeshBuilder::UpdateComponentMesh(
		DynamicMeshComp,
		HeightBuffer,
		Resolution,
		WorldSize,
		ZScale,
		bGenerateCollision
	);

	bInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d]: Ready (DEM Fast Mesh)"), GridCoordinate.X, GridCoordinate.Y);
}

bool ATerraDyneChunk::SetupGPU()
{
	if (!BrushMaterialBase)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Chunk [%d,%d]: No brush material — GPU disabled"), GridCoordinate.X, GridCoordinate.Y);
		bUseGPU = false;
		return false;
	}

	// Create ping-pong render targets
	HeightRT = NewObject<UTextureRenderTarget2D>(this);
	HeightRT->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, false);
	HeightRT->UpdateResource();

	HeightRT_Swap = NewObject<UTextureRenderTarget2D>(this);
	HeightRT_Swap->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, false);
	HeightRT_Swap->UpdateResource();

	// Upload CPU height data to HeightRT and wait for completion
	UpdateRenderTargetFromHeightmap();
	FlushRenderingCommands();

	// Validate: readback should match the uploaded data within tolerance
	FTextureRenderTargetResource* RTResource = HeightRT->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: RT resource unavailable — GPU disabled"), GridCoordinate.X, GridCoordinate.Y);
		bUseGPU = false;
		return false;
	}

	TArray<FLinearColor> Readback;
	if (!RTResource->ReadLinearColorPixels(Readback) || Readback.Num() != HeightBuffer.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: RT readback size mismatch — GPU disabled"), GridCoordinate.X, GridCoordinate.Y);
		bUseGPU = false;
		return false;
	}

	// Spot-check a few samples to verify data integrity
	float MaxError = 0.f;
	int32 SampleStride = FMath::Max(1, Readback.Num() / 16);
	for (int32 i = 0; i < Readback.Num(); i += SampleStride)
	{
		MaxError = FMath::Max(MaxError, FMath::Abs(Readback[i].R - HeightBuffer[i]));
	}

	if (MaxError > 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: RT validation failed (max error %.4f) — GPU disabled"),
			GridCoordinate.X, GridCoordinate.Y, MaxError);
		bUseGPU = false;
		return false;
	}

	bUseGPU = true;
	return true;
}

void ATerraDyneChunk::UpdateRenderTargetFromHeightmap()
{
	if (!HeightRT || HeightBuffer.Num() != Resolution * Resolution) return;

	FTextureRenderTargetResource* RenderTargetResource = HeightRT->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		HeightRT->UpdateResourceImmediate(false);
		FlushRenderingCommands();
		RenderTargetResource = HeightRT->GameThread_GetRenderTargetResource();
		if (!RenderTargetResource)
		{
			return;
		}
	}

	FTextureRHIRef RenderTargetTexture = RenderTargetResource->GetRenderTargetTexture();
	if (!RenderTargetTexture.IsValid())
	{
		return;
	}

	TArray<FFloat16Color> PixelData;
	PixelData.SetNumUninitialized(Resolution * Resolution);
	for (int32 i = 0; i < HeightBuffer.Num(); i++)
	{
		PixelData[i] = FFloat16Color(FLinearColor(HeightBuffer[i], HeightBuffer[i], HeightBuffer[i], 1.f));
	}

	ENQUEUE_RENDER_COMMAND(TerraDyneUploadHeightRT)(
		[Texture = MoveTemp(RenderTargetTexture), Data = MoveTemp(PixelData), Res = Resolution](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TextureResource = Texture.GetReference();
			if (!TextureResource) return;
			uint32 Stride = 0;
			void* MipData = RHICmdList.LockTexture2D(TextureResource, 0, RLM_WriteOnly, Stride, false);
			if (MipData)
			{
				// Copy row-by-row respecting GPU row pitch (Stride may include padding)
				const int32 BytesPerRow = Res * sizeof(FFloat16Color);
				for (int32 Row = 0; Row < Res; Row++)
				{
					FMemory::Memcpy(
						static_cast<uint8*>(MipData) + Row * Stride,
						reinterpret_cast<const uint8*>(Data.GetData()) + Row * BytesPerRow,
						BytesPerRow);
				}
				RHICmdList.UnlockTexture2D(TextureResource, 0, false);
			}
		});
}

void ATerraDyneChunk::SetMaterial(UMaterialInterface* InMaterial)
{
	if (!DynamicMeshComp) return;

	if (InMaterial)
	{
		// SetMaterial() can run before InitializeChunk() (e.g. RegisterChunk binds the material
		// right after spawn), so create the height source here if it does not exist yet — without
		// it the HeightMap parameter falls back to the material's own default texture.
		if (!CPUHeightTexture && !HeightRT)
		{
			CPUHeightTexture = CreateChunkDataTexture(Resolution, PF_FloatRGBA, /*bSRGB=*/false, TEXT("CPUHeightTexture"));
		}

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(InMaterial, this);
		if (MID)
		{
			UTexture* EffectiveHeightTexture = HeightRT ? (UTexture*)HeightRT : (UTexture*)CPUHeightTexture;
			ApplyTerrainMaterialParameters(MID, EffectiveHeightTexture, WorldSize, ZScale, Resolution);
			DynamicMeshComp->SetMaterial(0, MID);
		}
	}
	else
	{
		UMaterialInterface* DefaultMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (DefaultMat)
		{
			DynamicMeshComp->SetMaterial(0, DefaultMat);
		}
	}
}

void ATerraDyneChunk::UpdateDisplayMaterialParameters()
{
	if (!DynamicMeshComp)
	{
		return;
	}

	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(DynamicMeshComp->GetMaterial(0)))
	{
		UTexture* EffectiveHeightTexture = HeightRT ? (UTexture*)HeightRT : (UTexture*)CPUHeightTexture;
		ApplyTerrainMaterialParameters(MID, EffectiveHeightTexture, WorldSize, ZScale, Resolution);
	}
}

void ATerraDyneChunk::UploadCPUHeightTexture()
{
	if (!CPUHeightTexture) return;

	const int32 NumPixels = Resolution * Resolution;
	if (NumPixels <= 0 || HeightBuffer.Num() != NumPixels)
	{
		return;
	}

	// Matches HeightRT's encoding (UpdateRenderTargetFromHeightmap): R=G=B=normalized height, A=1.
	// FP16 rather than 8-bit — see CreateChunkDataTexture for why the precision matters to the
	// normal the material finite-differences out of this texture.
	const int32 BytesPerPixel = sizeof(FFloat16Color);
	FFloat16Color* PixelsCopy = (FFloat16Color*)FMemory::Malloc(NumPixels * BytesPerPixel);

	for (int32 i = 0; i < NumPixels; i++)
	{
		const float H = HeightBuffer[i];
		PixelsCopy[i] = FFloat16Color(FLinearColor(H, H, H, 1.f));
	}

	FUpdateTextureRegion2D* Region = (FUpdateTextureRegion2D*)FMemory::Malloc(sizeof(FUpdateTextureRegion2D));
	*Region = FUpdateTextureRegion2D(0, 0, 0, 0, Resolution, Resolution);

	CPUHeightTexture->UpdateTextureRegions(
		0,
		1,
		Region,
		Resolution * BytesPerPixel,
		BytesPerPixel,
		(uint8*)PixelsCopy,
		[](uint8* DataPtr, const FUpdateTextureRegion2D* RegionPtr)
		{
			FMemory::Free(DataPtr);
			FMemory::Free((void*)RegionPtr);
		}
	);

	UpdateDisplayMaterialParameters();
}

void ATerraDyneChunk::ApplyLocalIdempotentEdit(
	FVector RelativePos, float Radius, float Strength,
	ETerraDyneBrushMode BrushMode,
	float FlattenHeight)
{
	// Every CPU brush function below divides by Radius for its falloff curve — a zero (or
	// non-finite) radius would inject NaN/Inf straight into the height buffer, which silently
	// makes the GPU rasterizer discard every triangle touching that vertex (a clean hole with
	// no error). Guard once here instead of in every brush function.
	if (!FMath::IsFinite(Radius) || Radius <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: ApplyLocalIdempotentEdit rejected invalid Radius=%.4f"),
			GridCoordinate.X, GridCoordinate.Y, Radius);
		return;
	}

	float HalfSize = WorldSize * 0.5f;
	bool bInBoundsX = RelativePos.X >= -HalfSize - Radius && RelativePos.X <= HalfSize + Radius;
	bool bInBoundsY = RelativePos.Y >= -HalfSize - Radius && RelativePos.Y <= HalfSize + Radius;
	if (!bInBoundsX || !bInBoundsY) return;

	UE_LOG(LogTemp, Verbose, TEXT("Chunk [%d,%d]: Brush Mode=%d at %s R=%.0f S=%.0f"),
		GridCoordinate.X, GridCoordinate.Y, (int32)BrushMode, *RelativePos.ToString(), Radius, Strength);

	bool bModifiedHeight = false;

	switch (BrushMode)
	{
	case ETerraDyneBrushMode::Raise:
		ApplyBrushCPUInternal(SculptBuffer, Resolution, WorldSize, RelativePos, Radius, FMath::Abs(Strength), ZScale);
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Lower:
		ApplyBrushCPUInternal(SculptBuffer, Resolution, WorldSize, RelativePos, Radius, -FMath::Abs(Strength), ZScale);
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Smooth:
		ApplySmoothCPUInternal(SculptBuffer, Resolution, WorldSize, RelativePos, Radius, FMath::Clamp(Strength / ZScale, 0.f, 1.f));
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Flatten:
		ApplyFlattenCPUInternal(SculptBuffer, BaseBuffer, DetailBuffer, Resolution, WorldSize,
			RelativePos, Radius, FMath::Clamp(Strength / ZScale, 0.f, 1.f),
			FlattenHeight / ZScale);
		bModifiedHeight = true;
		break;

	default:
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: Unknown BrushMode %d — no-op"),
			GridCoordinate.X, GridCoordinate.Y, (int32)BrushMode);
		break;
	}

	if (bModifiedHeight)
	{
		for (int32 i = 0; i < SculptBuffer.Num(); i++)
		{
			SculptBuffer[i] = FMath::Clamp(SculptBuffer[i], -1.f, 1.f);
		}
		RecomputeHeightFromLayers();

		// This runs every frame while a brush stroke is held down — topology never changes
		// during sculpting (only height), so update vertex positions in place instead of paying
		// for a full Clear()+re-triangulate on every single tick.
		NotifyHeightBufferChanged();
	}
}

void ATerraDyneChunk::ApplyBrushGPU(FVector LocalPos, float Radius, float Strength)
{
	if (!HeightRT || !HeightRT_Swap || !BrushMaterialBase) return;

	float HalfSize = WorldSize * 0.5f;
	float U = (LocalPos.X + HalfSize) / WorldSize;
	float V = (LocalPos.Y + HalfSize) / WorldSize;
	float UVRadius = Radius / WorldSize;

	UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BrushMaterialBase, this);
	if (!DynMat) return;

	DynMat->SetScalarParameterValue(TEXT("Radius"), UVRadius);
	DynMat->SetScalarParameterValue(TEXT("Strength"), Strength / ZScale);
	DynMat->SetVectorParameterValue(TEXT("BrushPos"), FLinearColor(U, V, 0, 0));
	// Ping-pong: material reads current height from HeightRT, writes result to HeightRT_Swap
	DynMat->SetTextureParameterValue(TEXT("PrevHeight"), HeightRT);

	UCanvas* Canvas = nullptr;
	FVector2D RTSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, HeightRT_Swap, Canvas, RTSize, Context);
	if (Canvas)
	{
		Canvas->K2_DrawMaterial(DynMat, FVector2D::ZeroVector, RTSize, FVector2D::ZeroVector, FVector2D::UnitVector);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	// Swap: HeightRT_Swap (just written) becomes the new current HeightRT
	Swap(HeightRT, HeightRT_Swap);

	UpdateDisplayMaterialParameters();

	ReadbackRenderTarget();
}

void ATerraDyneChunk::ReadbackRenderTarget()
{
	if (!HeightRT) return;
	
	FlushRenderingCommands();
	
	FTextureRenderTargetResource* RTResource = HeightRT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;
	
	TArray<FLinearColor> Output;
	if (RTResource->ReadLinearColorPixels(Output) && Output.Num() == HeightBuffer.Num())
	{
		for (int32 i = 0; i < Output.Num(); i++)
		{
			HeightBuffer[i] = Output[i].R;
		}
	}
}

void ATerraDyneChunk::RebuildMesh()
{
	if (!DynamicMeshComp) return;
	if (Resolution < 2)
	{
		return;
	}

	const int32 ExpectedSamples = Resolution * Resolution;
	if (HeightBuffer.Num() != ExpectedSamples)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: RebuildMesh skipped due to invalid sample count (%d vs %d)."),
			GridCoordinate.X, GridCoordinate.Y, HeightBuffer.Num(), ExpectedSamples);
		return;
	}

	DynamicMeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		Mesh.Clear();
		Mesh.EnableAttributes();

		UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();

		const double Step = (double)WorldSize / (double)(Resolution - 1);
		const double Offset = (double)WorldSize * -0.5;
		const double UVWorldTileSize = 1000.0;
		const FVector ChunkWorldOrigin = GetActorLocation();

		// --- Vertices + UV elements (single top surface — no bottom slab) ---
		for (int32 Y = 0; Y < Resolution; Y++) {
			for (int32 X = 0; X < Resolution; X++) {
				double PX = Offset + (X * Step);
				double PY = Offset + (Y * Step);
				float Height = HeightBuffer[Y * Resolution + X];
				if (!FMath::IsFinite(Height))
				{
					// A non-finite height (NaN/Inf, e.g. from a divide-by-zero in brush math)
					// silently produces a NaN vertex position, which the GPU rasterizer discards
					// entirely — every triangle touching it vanishes with no error, leaving a
					// clean contiguous hole in the mesh. Sanitize so that can never happen.
					UE_LOG(LogTemp, Warning,
						TEXT("Chunk [%d,%d]: Non-finite HeightBuffer value at [%d,%d] — clamped to 0."),
						GridCoordinate.X, GridCoordinate.Y, X, Y);
					Height = 0.0f;
				}
				Mesh.AppendVertex(FVector3d(PX, PY, Height * ZScale));

				// UVs derived from absolute WORLD position (not chunk-local X/Y) so the tiled
				// pattern is continuous across chunk boundaries instead of restarting at 0 per
				// chunk, which is what made every chunk edge show a visible seam in the material.
				float U = (float)((ChunkWorldOrigin.X + PX) / UVWorldTileSize);
				float V = (float)((ChunkWorldOrigin.Y + PY) / UVWorldTileSize);
				UVOverlay->AppendElement(FVector2f(U, V));
			}
		}

		// --- Triangles (wire UV overlay to each triangle) ---
		for (int32 Y = 0; Y < Resolution - 1; Y++) {
			for (int32 X = 0; X < Resolution - 1; X++) {
				int32 BL = Y * Resolution + X;
				int32 BR = BL + 1;
				int32 TL = (Y + 1) * Resolution + X;
				int32 TR = TL + 1;

				// Winding reversed (BL,TR,BR / BL,TL,TR) so QuickComputeVertexNormals derives
				// upward-facing normals instead of the downward ones that made the terrain
				// render near-black (lit as if from below).
				int32 T0 = Mesh.AppendTriangle(BL, TR, BR);
				int32 T1 = Mesh.AppendTriangle(BL, TL, TR);
				if (T0 >= 0)
				{
					UVOverlay->SetTriangle(T0, UE::Geometry::FIndex3i(BL, TR, BR));
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("Chunk [%d,%d]: AppendTriangle(BL=%d,TR=%d,BR=%d) FAILED with code %d at grid [%d,%d]"),
						GridCoordinate.X, GridCoordinate.Y, BL, TR, BR, T0, X, Y);
				}
				if (T1 >= 0)
				{
					UVOverlay->SetTriangle(T1, UE::Geometry::FIndex3i(BL, TL, TR));
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("Chunk [%d,%d]: AppendTriangle(BL=%d,TL=%d,TR=%d) FAILED with code %d at grid [%d,%d]"),
						GridCoordinate.X, GridCoordinate.Y, BL, TL, TR, T1, X, Y);
				}
			}
		}

		// Populate the overlay normals explicitly so DynamicMesh builds stable lighting and tangents.
		UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Mesh);
		if (NormalOverlay)
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
		}

		// Bake the layer split into vertex colors now that the normals it depends on exist.
		ApplyLayerWeightsToMesh(Mesh, HeightBuffer, Resolution);
	});

	if (HeightRT)
	{
		UpdateRenderTargetFromHeightmap();
	}

	UpdateDisplayMaterialParameters();
}

void ATerraDyneChunk::UpdateMeshHeights()
{
	if (!DynamicMeshComp) return;
	if (Resolution < 2) return;

	const int32 ExpectedSamples = Resolution * Resolution;
	if (HeightBuffer.Num() != ExpectedSamples)
	{
		return;
	}

	bool bTopologyMatched = false;

	DynamicMeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		// Vertex IDs are dense and sequential (Y*Resolution+X) as long as the mesh was last
		// built by RebuildMesh()'s Clear()+AppendVertex loop with no deletions in between, which
		// holds for this component. If that assumption ever breaks (resolution changed, mesh not
		// built yet), bail out here and let the caller fall back to a full RebuildMesh().
		if (Mesh.VertexCount() != ExpectedSamples || Mesh.MaxVertexID() != ExpectedSamples)
		{
			return;
		}
		bTopologyMatched = true;

		const double Step = (double)WorldSize / (double)(Resolution - 1);
		const double Offset = (double)WorldSize * -0.5;

		for (int32 Y = 0; Y < Resolution; Y++)
		{
			for (int32 X = 0; X < Resolution; X++)
			{
				const int32 VID = Y * Resolution + X;
				double PX = Offset + (X * Step);
				double PY = Offset + (Y * Step);
				float Height = HeightBuffer[VID];
				if (!FMath::IsFinite(Height))
				{
					Height = 0.0f;
				}
				Mesh.SetVertex(VID, FVector3d(PX, PY, Height * ZScale));
			}
		}

		UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Mesh);
		if (UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals())
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
		}

	});

	// Sculpting changes both height and slope, so the layer split still has to be recomputed — but
	// on a debounce rather than inline, since this function runs every frame of a held stroke.
	MarkLayerWeightsDirty();

	if (!bTopologyMatched)
	{
		RebuildMesh();
		return;
	}

	if (DynamicMeshComp)
	{
		DynamicMeshComp->NotifyMeshVertexAttributesModified(true, true);
	}

	UpdateDisplayMaterialParameters();
}

void ATerraDyneChunk::MarkLayerWeightsDirty()
{
	bLayerWeightsDirty = true;
	LayerWeightsDebounceTimer = 0.15f;
}

void ATerraDyneChunk::RebakeLayerWeights()
{
	if (!DynamicMeshComp || Resolution < 2)
	{
		return;
	}

	const int32 ExpectedSamples = Resolution * Resolution;
	if (HeightBuffer.Num() != ExpectedSamples)
	{
		return;
	}

	// Colour only — vertex positions and normals are already current from UpdateMeshHeights.
	DynamicMeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		if (Mesh.VertexCount() != ExpectedSamples)
		{
			return;
		}
		ApplyLayerWeightsToMesh(Mesh, HeightBuffer, Resolution);
	});
}

void ATerraDyneChunk::UpdateCollision()
{
	if (!DynamicMeshComp) return;
	DynamicMeshComp->UpdateCollision(true);
}

void ATerraDyneChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bCollisionDirty)
	{
		CollisionDebounceTimer -= DeltaTime;
		if (CollisionDebounceTimer <= 0.0f)
		{
			UpdateCollision();
			bCollisionDirty = false;
		}
	}

	if (bLayerWeightsDirty)
	{
		LayerWeightsDebounceTimer -= DeltaTime;
		if (LayerWeightsDebounceTimer <= 0.0f)
		{
			RebakeLayerWeights();
			bLayerWeightsDirty = false;
		}
	}

	if (bTransferredFoliageDirty)
	{
		TransferredFoliageDebounceTimer -= DeltaTime;
		if (TransferredFoliageDebounceTimer <= 0.0f)
		{
			RefreshTransferredFoliagePlacement(true);
			RefreshTransferredActorFoliagePlacement(true);
		}
	}
}

void ATerraDyneChunk::RebuildPhysicsMesh()
{
	RebuildMesh();
	UpdateCollision();
}

FBox ATerraDyneChunk::GetWorldBounds() const
{
	const FVector Extent(WorldSize * 0.5f, WorldSize * 0.5f, FMath::Max(1000.0f, ZScale));
	return FBox::BuildAABB(GetActorLocation(), Extent);
}

void ATerraDyneChunk::RecomputeHeightFromLayers()
{
	const int32 Count = HeightBuffer.Num();
	if (Count <= 0 ||
		BaseBuffer.Num() != Count ||
		SculptBuffer.Num() != Count ||
		DetailBuffer.Num() != Count)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: RecomputeHeightFromLayers skipped — layer sizes disagree (H=%d B=%d S=%d D=%d)."),
			GridCoordinate.X, GridCoordinate.Y,
			Count, BaseBuffer.Num(), SculptBuffer.Num(), DetailBuffer.Num());
		return;
	}

	const bool bHasPad = (PadWeightBuffer.Num() == Count && PadTargetBuffer.Num() == Count);

	for (int32 i = 0; i < Count; i++)
	{
		float Height = BaseBuffer[i] + SculptBuffer[i] + DetailBuffer[i];

		if (bHasPad)
		{
			const float Weight = PadWeightBuffer[i];
			if (Weight > 0.0f)
			{
				Height = FMath::Lerp(Height, PadTargetBuffer[i], FMath::Min(Weight, 1.0f));
			}
		}

		HeightBuffer[i] = FMath::Clamp(Height, 0.0f, 1.0f);
	}
}

void ATerraDyneChunk::NotifyHeightBufferChanged()
{
	UpdateMeshHeights();
	if (!bUseGPU)
	{
		UploadCPUHeightTexture();
	}

	float DebounceTime = 0.2f;
	float FoliageDelay = 0.5f;
	if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
	{
		DebounceTime = Settings->CollisionDebounceTime;
		FoliageDelay = Settings->FoliageDebounceTime;
	}

	bCollisionDirty = true;
	CollisionDebounceTimer = DebounceTime;

	if (bTransferredFoliageFollowsTerrain &&
		(TransferredFoliageInstanceLocalTransforms.Num() > 0 ||
		 TransferredActorFoliageInstanceLocalTransforms.Num() > 0))
	{
		bTransferredFoliageDirty = true;
		TransferredFoliageDebounceTimer = FoliageDelay;
	}
}

void ATerraDyneChunk::ClearBuildingPad()
{
	bHasBuildingPad = false;

	const int32 Count = Resolution * Resolution;
	if (Count <= 0)
	{
		PadWeightBuffer.Reset();
		PadTargetBuffer.Reset();
		return;
	}

	PadWeightBuffer.Init(0.0f, Count);
	PadTargetBuffer.Init(0.0f, Count);
}

void ATerraDyneChunk::StampBuildingPad(
	const TArray<FVector2D>& LocalPolygonXY, float TargetLocalZ, float Falloff)
{
	if (LocalPolygonXY.Num() < 3 || Resolution < 2 || WorldSize <= 0.0f)
	{
		return;
	}

	// ZScale is the divisor that takes world units into the normalized 0-1 height space the
	// buffers work in. A zero or non-finite one would put NaN straight into PadTargetBuffer, and
	// a NaN vertex position is silently dropped by the rasterizer — a hole with no error message.
	if (!FMath::IsFinite(ZScale) || ZScale <= 0.0f || !FMath::IsFinite(TargetLocalZ))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: StampBuildingPad rejected (ZScale=%.4f TargetLocalZ=%.4f)."),
			GridCoordinate.X, GridCoordinate.Y, ZScale, TargetLocalZ);
		return;
	}

	const int32 Count = Resolution * Resolution;
	if (PadWeightBuffer.Num() != Count || PadTargetBuffer.Num() != Count)
	{
		ClearBuildingPad();
	}

	const float SafeFalloff = FMath::Max(Falloff, 1.0f);
	const float TargetNorm = TargetLocalZ / ZScale;

	// Only walk the grid the footprint plus its skirt can actually reach.
	FBox2D Area(ForceInit);
	for (const FVector2D& P : LocalPolygonXY)
	{
		Area += P;
	}
	Area = Area.ExpandBy(SafeFalloff);

	const float Half = WorldSize * 0.5f;
	const float Step = WorldSize / (Resolution - 1);

	const int32 MinX = FMath::Clamp(FMath::FloorToInt((Area.Min.X + Half) / Step), 0, Resolution - 1);
	const int32 MaxX = FMath::Clamp(FMath::CeilToInt((Area.Max.X + Half) / Step), 0, Resolution - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt((Area.Min.Y + Half) / Step), 0, Resolution - 1);
	const int32 MaxY = FMath::Clamp(FMath::CeilToInt((Area.Max.Y + Half) / Step), 0, Resolution - 1);

	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			const FVector2D LocalPos(-Half + X * Step, -Half + Y * Step);
			const float Dist = SignedDistanceToPolygon2D(LocalPos, LocalPolygonXY);

			float Weight;
			if (Dist <= 0.0f)
			{
				Weight = 1.0f;               // inside the footprint: fully flat
			}
			else if (Dist >= SafeFalloff)
			{
				continue;                    // past the skirt: untouched ground
			}
			else
			{
				// Same smoothstep the sculpt brushes use, so a pad edge reads like a brush edge.
				const float T = 1.0f - (Dist / SafeFalloff);
				Weight = T * T * (3.0f - 2.0f * T);
			}

			const int32 Idx = Y * Resolution + X;
			if (Weight > PadWeightBuffer[Idx])
			{
				PadWeightBuffer[Idx] = Weight;
				PadTargetBuffer[Idx] = TargetNorm;
				bHasBuildingPad = true;
			}
		}
	}
}

float ATerraDyneChunk::GetNaturalHeightAtLocation(FVector LocalPos) const
{
	// Same sampling as GetHeightAtLocation but against the un-padded layers, so that auto-levelling
	// a building reads the real ground rather than a pad already stamped there — including its own
	// from a previous refresh, which would otherwise let the target drift on every recompute.
	const int32 Count = Resolution * Resolution;
	if (Resolution < 2 ||
		BaseBuffer.Num() != Count || SculptBuffer.Num() != Count || DetailBuffer.Num() != Count)
	{
		return 0.0f;
	}

	const float HalfSize = WorldSize * 0.5f;
	const float GridX = ((LocalPos.X + HalfSize) / WorldSize) * (Resolution - 1);
	const float GridY = ((LocalPos.Y + HalfSize) / WorldSize) * (Resolution - 1);

	const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, Resolution - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, Resolution - 1);
	const int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);

	const float FX = FMath::Clamp(GridX - X0, 0.0f, 1.0f);
	const float FY = FMath::Clamp(GridY - Y0, 0.0f, 1.0f);

	auto Natural = [this](int32 Index) -> float
	{
		return FMath::Clamp(BaseBuffer[Index] + SculptBuffer[Index] + DetailBuffer[Index], 0.0f, 1.0f);
	};

	const float H00 = Natural(Y0 * Resolution + X0);
	const float H10 = Natural(Y0 * Resolution + X1);
	const float H01 = Natural(Y1 * Resolution + X0);
	const float H11 = Natural(Y1 * Resolution + X1);

	const float Height = (FY <= FX)
		? H00 + FX * (H10 - H00) + FY * (H11 - H10)
		: H00 + FX * (H11 - H01) + FY * (H01 - H00);

	return Height * ZScale;
}

float ATerraDyneChunk::GetHeightAtLocation(FVector LocalPos) const
{
	if (Resolution < 2 || HeightBuffer.Num() != Resolution * Resolution)
	{
		return 0.0f;
	}

	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);
	
	float NormX = (LocalPos.X + HalfSize) / WorldSize;
	float NormY = (LocalPos.Y + HalfSize) / WorldSize;
	
	float GridX = NormX * (Resolution - 1);
	float GridY = NormY * (Resolution - 1);
	
	int32 X0 = FMath::FloorToInt(GridX);
	int32 Y0 = FMath::FloorToInt(GridY);
	int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
	int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);
	
	float FX = GridX - X0;
	float FY = GridY - Y0;
	
	float H00 = HeightBuffer[FMath::Clamp(Y0 * Resolution + X0, 0, HeightBuffer.Num() - 1)];
	float H10 = HeightBuffer[FMath::Clamp(Y0 * Resolution + X1, 0, HeightBuffer.Num() - 1)];
	float H01 = HeightBuffer[FMath::Clamp(Y1 * Resolution + X0, 0, HeightBuffer.Num() - 1)];
	float H11 = HeightBuffer[FMath::Clamp(Y1 * Resolution + X1, 0, HeightBuffer.Num() - 1)];

	// Triangle-aware interpolation matching mesh winding (BL→BR→TR / BL→TR→TL)
	float Height;
	if (FY <= FX)
		Height = H00 + FX * (H10 - H00) + FY * (H11 - H10);
	else
		Height = H00 + FX * (H11 - H01) + FY * (H01 - H00);

	return Height * ZScale;
}

TArray<FTerraDyneScatterPoint> ATerraDyneChunk::GetScatterPoints(int32 SamplesPerAxis) const
{
	TArray<FTerraDyneScatterPoint> Points;
	if (SamplesPerAxis < 2 || Resolution < 2 || HeightBuffer.Num() != Resolution * Resolution)
	{
		return Points;
	}

	const float HalfSize = WorldSize * 0.5f;
	const float Step = WorldSize / (SamplesPerAxis - 1);
	const float NormalSampleOffset = FMath::Max(WorldSize / (Resolution - 1), 1.0f);

	Points.Reserve(SamplesPerAxis * SamplesPerAxis);

	for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
	{
		for (int32 X = 0; X < SamplesPerAxis; ++X)
		{
			const FVector LocalPos(X * Step - HalfSize, Y * Step - HalfSize, 0.0f);

			const float HCenter = GetHeightAtLocation(LocalPos);
			const float HRight = GetHeightAtLocation(LocalPos + FVector(NormalSampleOffset, 0.0f, 0.0f));
			const float HForward = GetHeightAtLocation(LocalPos + FVector(0.0f, NormalSampleOffset, 0.0f));

			const FVector TangentX(NormalSampleOffset, 0.0f, HRight - HCenter);
			const FVector TangentY(0.0f, NormalSampleOffset, HForward - HCenter);
			const FVector Normal = FVector::CrossProduct(TangentY, TangentX).GetSafeNormal();

			const float NormX = (LocalPos.X + HalfSize) / WorldSize;
			const float NormY = (LocalPos.Y + HalfSize) / WorldSize;
			const int32 GX = FMath::Clamp(FMath::RoundToInt(NormX * (Resolution - 1)), 0, Resolution - 1);
			const int32 GY = FMath::Clamp(FMath::RoundToInt(NormY * (Resolution - 1)), 0, Resolution - 1);
			const int32 Idx = GY * Resolution + GX;

			FTerraDyneScatterPoint Point;
			Point.WorldLocation = GetActorTransform().TransformPosition(FVector(LocalPos.X, LocalPos.Y, HCenter));
			Point.Normal = Normal;
			Point.Slope01 = FMath::Clamp(1.0f - Normal.Z, 0.0f, 1.0f);
			Points.Add(Point);
		}
	}

	return Points;
}

FTerraDyneChunkData ATerraDyneChunk::GetSerializedData()
{
	FTerraDyneChunkData Data;
	Data.Coordinate = GridCoordinate;
	Data.ZScale = ZScale;

	// NOTE: ApplyBrushGPU() (the only code that would keep HeightRT in sync with actual
	// sculpting) is currently unused — all real sculpting goes through the CPU path
	// (ApplyLocalIdempotentEdit) and never touches HeightRT after its one-time setup upload.
	// Reading it back here would overwrite the correctly-sculpted CPU HeightBuffer with that
	// stale pre-sculpt snapshot, reverting this chunk to its original shape on every streaming
	// unload/save and creating a discontinuity with neighboring chunks. CPU HeightBuffer is the
	// only authoritative source right now, so this readback must not run.

	int32 SerializedResolution = Resolution;
	int32 SerializedSamples = SerializedResolution * SerializedResolution;
	if (HeightBuffer.Num() != SerializedSamples)
	{
		const int32 DerivedResolution = ResolveSquareResolutionFromSampleCount(HeightBuffer.Num());
		if (DerivedResolution > 1)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Serializing with derived resolution %d because HeightBuffer has %d samples but Resolution is %d."),
				GridCoordinate.X, GridCoordinate.Y, DerivedResolution, HeightBuffer.Num(), Resolution);
			SerializedResolution = DerivedResolution;
			SerializedSamples = SerializedResolution * SerializedResolution;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Invalid non-square HeightBuffer (%d samples). Serializing flat fallback for resolution %d."),
				GridCoordinate.X, GridCoordinate.Y, HeightBuffer.Num(), Resolution);
			SerializedResolution = FMath::Max(Resolution, 2);
			SerializedSamples = SerializedResolution * SerializedResolution;
		}
	}

	Data.Resolution = SerializedResolution;
	Data.HeightData = HeightBuffer.Num() == SerializedSamples
		? HeightBuffer
		: TArray<float>();
	if (Data.HeightData.Num() != SerializedSamples)
	{
		Data.HeightData.Init(0.0f, SerializedSamples);
	}

	Data.BaseData = BaseBuffer.Num() == SerializedSamples ? BaseBuffer : Data.HeightData;
	Data.SculptData = SculptBuffer.Num() == SerializedSamples ? SculptBuffer : TArray<float>();
	Data.DetailData = DetailBuffer.Num() == SerializedSamples ? DetailBuffer : TArray<float>();
	if (Data.SculptData.Num() != SerializedSamples)
	{
		Data.SculptData.Init(0.0f, SerializedSamples);
	}
	if (Data.DetailData.Num() != SerializedSamples)
	{
		Data.DetailData.Init(0.0f, SerializedSamples);
	}

	Data.bTransferredFoliageFollowsTerrain = bTransferredFoliageFollowsTerrain;
	Data.FoliageStaticMeshPaths = TransferredFoliageStaticMeshPaths;
	Data.FoliageMaterialCounts = TransferredFoliageMaterialCounts;
	Data.FoliageOverrideMaterialPaths = TransferredFoliageOverrideMaterialPaths;
	Data.FoliageDefinitionIndices = TransferredFoliageDefinitionIndices;
	Data.FoliageInstanceLocalTransforms = TransferredFoliageInstanceLocalTransforms;
	Data.FoliageInstanceTerrainOffsets = TransferredFoliageInstanceTerrainOffsets;
	Data.ActorFoliageClassPaths = TransferredActorFoliageClassPaths;
	Data.ActorFoliageAttachFlags = TransferredActorFoliageAttachFlags;
	Data.ActorFoliageDefinitionIndices = TransferredActorFoliageDefinitionIndices;
	Data.ActorFoliageInstanceLocalTransforms = TransferredActorFoliageInstanceLocalTransforms;
	Data.ActorFoliageInstanceTerrainOffsets = TransferredActorFoliageInstanceTerrainOffsets;

	return Data;
}

void ATerraDyneChunk::LoadFromData(const FTerraDyneChunkData& Data)
{
	int32 LoadResolution = Data.Resolution;
	int32 ExpectedPx = LoadResolution * LoadResolution;
	if (LoadResolution < 2 || Data.HeightData.Num() != ExpectedPx)
	{
		const int32 DerivedResolution = ResolveSquareResolutionFromSampleCount(Data.HeightData.Num());
		if (DerivedResolution > 1)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: LoadFromData corrected resolution %d -> %d from %d serialized samples."),
				GridCoordinate.X, GridCoordinate.Y, Data.Resolution, DerivedResolution, Data.HeightData.Num());
			LoadResolution = DerivedResolution;
			ExpectedPx = LoadResolution * LoadResolution;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: LoadFromData received invalid height payload (%d samples for resolution %d). Falling back to flat terrain."),
				GridCoordinate.X, GridCoordinate.Y, Data.HeightData.Num(), Data.Resolution);
			LoadResolution = FMath::Max(Data.Resolution, 2);
			ExpectedPx = LoadResolution * LoadResolution;
		}
	}

	if (LoadResolution != Resolution || !bInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d] Resolution mismatch on load (%d → %d). Re-initializing."),
			GridCoordinate.X, GridCoordinate.Y, Resolution, LoadResolution);
		bInitialized = false;
		Initialize(LoadResolution, WorldSize);
	}

	GridCoordinate = Data.Coordinate;
	// FTerraDyneChunkData::ZScale defaults to 0.0f when unset (e.g. stale/partial save data).
	// ZScale is a divisor in the brush math (Strength/ZScale, FlattenHeight/ZScale) — loading a
	// zero here would inject NaN/Inf into every subsequent brush stroke on this chunk, which
	// silently makes affected triangles vanish from rendering. Keep the chunk's current ZScale
	// (set to 3000 at construction) rather than accepting an invalid loaded value.
	if (FMath::IsFinite(Data.ZScale) && Data.ZScale > 0.0f)
	{
		ZScale = Data.ZScale;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: LoadFromData rejected invalid ZScale=%.4f, keeping %.4f"),
			GridCoordinate.X, GridCoordinate.Y, Data.ZScale, ZScale);
	}
	HeightBuffer = Data.HeightData.Num() == ExpectedPx ? Data.HeightData : TArray<float>();
	if (HeightBuffer.Num() != ExpectedPx)
	{
		HeightBuffer.Init(0.0f, ExpectedPx);
	}

	// Restore individual height layers; fall back to collapsing into BaseBuffer for old saves
	if (Data.BaseData.Num() == ExpectedPx)
	{
		BaseBuffer = Data.BaseData;
		SculptBuffer = Data.SculptData.Num() == ExpectedPx ? Data.SculptData : TArray<float>();
		DetailBuffer = Data.DetailData.Num() == ExpectedPx ? Data.DetailData : TArray<float>();
		if (SculptBuffer.Num() != ExpectedPx) SculptBuffer.Init(0.f, ExpectedPx);
		if (DetailBuffer.Num() != ExpectedPx) DetailBuffer.Init(0.f, ExpectedPx);
	}
	else
	{
		BaseBuffer = HeightBuffer;
		SculptBuffer.Init(0.0f, ExpectedPx);
		DetailBuffer.Init(0.0f, ExpectedPx);
	}

	// Data.HeightData was saved with any building pad already baked into it. Recomposing from the
	// layers here throws that away and rebuilds it from the live pad buffers instead, so a building
	// that has since moved or been deleted cannot leave its old pad frozen into a reloaded chunk.
	// Pads themselves are re-stamped by ATerraDyneManager on registration — see RegisterChunk.
	if (PadWeightBuffer.Num() != ExpectedPx || PadTargetBuffer.Num() != ExpectedPx)
	{
		ClearBuildingPad();
	}
	RecomputeHeightFromLayers();

	if (bUseGPU)
	{
		UpdateRenderTargetFromHeightmap();
		FlushRenderingCommands();
	}
	else
	{
		UploadCPUHeightTexture();
	}

	RebuildMesh();
	UpdateCollision();
	SetTransferredFoliageData(
		Data.FoliageStaticMeshPaths,
		Data.FoliageMaterialCounts,
		Data.FoliageOverrideMaterialPaths,
		Data.FoliageDefinitionIndices,
		Data.FoliageInstanceLocalTransforms,
		Data.FoliageInstanceTerrainOffsets,
		Data.bTransferredFoliageFollowsTerrain);
	SetTransferredActorFoliageData(
		Data.ActorFoliageClassPaths,
		Data.ActorFoliageAttachFlags,
		Data.ActorFoliageDefinitionIndices,
		Data.ActorFoliageInstanceLocalTransforms,
		Data.ActorFoliageInstanceTerrainOffsets);
}

void ATerraDyneChunk::UpdateLOD(const FVector& ViewerPos)
{
	// Simple distance-based LOD
	if (!DynamicMeshComp) return;

	const float Dist = FVector::Dist(ViewerPos, GetActorLocation());
	float ThresholdSq = 2500000000.0f; // Default 500m
	float ShadowCullDistance = 30000.0f;

	if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
	{
		float Thresh = Settings->LODDistanceThreshold;
		ThresholdSq = Thresh * Thresh;
		ShadowCullDistance = Settings->ChunkShadowCullDistance;
	}

	if (Dist * Dist > ThresholdSq)
	{
		if (DynamicMeshComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			DynamicMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			// Optional: Reduce mesh resolution here if we had a simplified mesh generator
		}
	}
	else
	{
		if (DynamicMeshComp->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
		{
			DynamicMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			// Force update if we just woke up
			UpdateCollision();
		}
	}

	// Dynamic shadow rendering is one of the most expensive costs per visible triangle — stop
	// paying it for chunks far enough away that the shadow contribution isn't perceptible.
	const bool bShouldCastShadow = Dist <= ShadowCullDistance;
	if (DynamicMeshComp->CastShadow != bShouldCastShadow)
	{
		DynamicMeshComp->SetCastShadow(bShouldCastShadow);
	}
}

void ATerraDyneChunk::SetTransferredFoliageData(
	const TArray<FString>& InStaticMeshPaths,
	const TArray<int32>& InMaterialCounts,
	const TArray<FString>& InOverrideMaterialPaths,
	const TArray<int32>& InDefinitionIndices,
	const TArray<FTransform>& InLocalTransforms,
	const TArray<float>& InTerrainOffsets,
	bool bInFollowsTerrain)
{
	TransferredFoliageStaticMeshPaths = InStaticMeshPaths;
	TransferredFoliageMaterialCounts = InMaterialCounts;
	TransferredFoliageOverrideMaterialPaths = InOverrideMaterialPaths;
	TransferredFoliageDefinitionIndices = InDefinitionIndices;
	TransferredFoliageInstanceLocalTransforms = InLocalTransforms;
	TransferredFoliageInstanceTerrainOffsets = InTerrainOffsets;
	bTransferredFoliageFollowsTerrain = bInFollowsTerrain;
	bTransferredFoliageDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;

	const int32 SharedInstanceCount = FMath::Min(
		TransferredFoliageDefinitionIndices.Num(),
		TransferredFoliageInstanceLocalTransforms.Num());
	if (SharedInstanceCount != TransferredFoliageDefinitionIndices.Num() ||
		SharedInstanceCount != TransferredFoliageInstanceLocalTransforms.Num())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: Trimming transferred foliage data to %d shared instances."),
			GridCoordinate.X, GridCoordinate.Y, SharedInstanceCount);
		TransferredFoliageDefinitionIndices.SetNum(SharedInstanceCount);
		TransferredFoliageInstanceLocalTransforms.SetNum(SharedInstanceCount);
	}

	if (TransferredFoliageInstanceTerrainOffsets.Num() != SharedInstanceCount)
	{
		TransferredFoliageInstanceTerrainOffsets.SetNumZeroed(SharedInstanceCount);
	}

	ClearTransferredFoliageComponents();
	RefreshTransferredFoliagePlacement(bTransferredFoliageFollowsTerrain);
}

void ATerraDyneChunk::RefreshTransferredFoliagePlacement(bool bSnapToTerrain)
{
	const int32 DefinitionCount = TransferredFoliageStaticMeshPaths.Num();
	const int32 InstanceCount = TransferredFoliageInstanceLocalTransforms.Num();
	if (DefinitionCount == 0 || InstanceCount == 0)
	{
		ClearTransferredFoliageComponents();
		bTransferredFoliageDirty = false;
		TransferredFoliageDebounceTimer = 0.0f;
		return;
	}

	const bool bNeedsRebuild = TransferredFoliageISMs.Num() != DefinitionCount;
	if (bNeedsRebuild)
	{
		ClearTransferredFoliageComponents();
		TransferredFoliageISMs.SetNum(DefinitionCount);

		int32 MaterialCursor = 0;
		for (int32 DefinitionIndex = 0; DefinitionIndex < DefinitionCount; DefinitionIndex++)
		{
			const FString& StaticMeshPath = TransferredFoliageStaticMeshPaths[DefinitionIndex];
			UStaticMesh* StaticMesh = StaticMeshPath.IsEmpty()
				? nullptr
				: LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);

			int32 MaterialCount = TransferredFoliageMaterialCounts.IsValidIndex(DefinitionIndex)
				? FMath::Max(0, TransferredFoliageMaterialCounts[DefinitionIndex])
				: 0;
			MaterialCount = FMath::Min(
				MaterialCount,
				FMath::Max(0, TransferredFoliageOverrideMaterialPaths.Num() - MaterialCursor));

			if (!StaticMesh)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Chunk [%d,%d]: Failed to load transferred foliage mesh '%s'."),
					GridCoordinate.X, GridCoordinate.Y, *StaticMeshPath);
				MaterialCursor += MaterialCount;
				continue;
			}

			UHierarchicalInstancedStaticMeshComponent* NewISM =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			NewISM->SetupAttachment(GetRootComponent());
			// Static instances get precomputed lighting/culling and cheaper render-thread batching;
			// only pay the Movable cost when the terrain-follow refresh path will actually reposition them.
			NewISM->SetMobility(bTransferredFoliageFollowsTerrain ? EComponentMobility::Movable : EComponentMobility::Static);
			NewISM->SetStaticMesh(StaticMesh);
			NewISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NewISM->SetCanEverAffectNavigation(false);
			NewISM->SetCastShadow(true);
			if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
			{
				NewISM->SetCullDistances(Settings->FoliageCullStartDistance, Settings->FoliageCullEndDistance);
			}

			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
			{
				const FString& MaterialPath = TransferredFoliageOverrideMaterialPaths[MaterialCursor + MaterialIndex];
				if (MaterialPath.IsEmpty())
				{
					continue;
				}

				if (UMaterialInterface* OverrideMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
				{
					NewISM->SetMaterial(MaterialIndex, OverrideMaterial);
				}
			}
			MaterialCursor += MaterialCount;

			NewISM->RegisterComponent();
			TransferredFoliageISMs[DefinitionIndex] = NewISM;
		}
	}
	if (bNeedsRebuild)
	{
		// First-time population: bucket transforms per-ISM and add each bucket in one batched call
		// instead of one AddInstance() per tree, so the HISM cluster tree is built once per mesh
		// instead of being incrementally reinserted-into on every single instance.
		TArray<TArray<FTransform>> PerISMTransforms;
		PerISMTransforms.SetNum(DefinitionCount);

		for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
		{
			const int32 DefinitionIndex = TransferredFoliageDefinitionIndices.IsValidIndex(InstanceIndex)
				? TransferredFoliageDefinitionIndices[InstanceIndex]
				: INDEX_NONE;
			if (!TransferredFoliageISMs.IsValidIndex(DefinitionIndex) || !TransferredFoliageISMs[DefinitionIndex])
			{
				continue;
			}

			FTransform InstanceTransform = TransferredFoliageInstanceLocalTransforms[InstanceIndex];
			if (bSnapToTerrain)
			{
				FVector LocalLocation = InstanceTransform.GetLocation();
				const float TerrainOffset = TransferredFoliageInstanceTerrainOffsets.IsValidIndex(InstanceIndex)
					? TransferredFoliageInstanceTerrainOffsets[InstanceIndex]
					: 0.0f;
				LocalLocation.Z = GetHeightAtLocation(FVector(LocalLocation.X, LocalLocation.Y, 0.0f)) + TerrainOffset;
				InstanceTransform.SetLocation(LocalLocation);
			}

			PerISMTransforms[DefinitionIndex].Add(InstanceTransform);
		}

		for (int32 DefinitionIndex = 0; DefinitionIndex < DefinitionCount; DefinitionIndex++)
		{
			if (UHierarchicalInstancedStaticMeshComponent* ISM = TransferredFoliageISMs[DefinitionIndex])
			{
				ISM->AddInstances(PerISMTransforms[DefinitionIndex], /*bShouldReturnIndices=*/false, /*bWorldSpace=*/false);
			}
		}
	}
	else
	{
		// Refresh of an already-built set: update existing instance slots in place instead of
		// clearing and re-adding everything, which previously threw away and rebuilt each HISM's
		// entire cluster tree every debounce tick while sculpting. Local index assignment mirrors
		// the deterministic per-ISM order used above, so slot N here is always the same instance.
		TArray<int32> PerISMCursor;
		PerISMCursor.SetNumZeroed(DefinitionCount);

		for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
		{
			const int32 DefinitionIndex = TransferredFoliageDefinitionIndices.IsValidIndex(InstanceIndex)
				? TransferredFoliageDefinitionIndices[InstanceIndex]
				: INDEX_NONE;
			if (!TransferredFoliageISMs.IsValidIndex(DefinitionIndex) || !TransferredFoliageISMs[DefinitionIndex])
			{
				continue;
			}

			UHierarchicalInstancedStaticMeshComponent* ISM = TransferredFoliageISMs[DefinitionIndex];

			FTransform InstanceTransform = TransferredFoliageInstanceLocalTransforms[InstanceIndex];
			if (bSnapToTerrain)
			{
				FVector LocalLocation = InstanceTransform.GetLocation();
				const float TerrainOffset = TransferredFoliageInstanceTerrainOffsets.IsValidIndex(InstanceIndex)
					? TransferredFoliageInstanceTerrainOffsets[InstanceIndex]
					: 0.0f;
				LocalLocation.Z = GetHeightAtLocation(FVector(LocalLocation.X, LocalLocation.Y, 0.0f)) + TerrainOffset;
				InstanceTransform.SetLocation(LocalLocation);
			}

			const int32 LocalIndex = PerISMCursor[DefinitionIndex]++;
			if (LocalIndex < ISM->GetInstanceCount())
			{
				ISM->UpdateInstanceTransform(LocalIndex, InstanceTransform, /*bWorldSpace=*/false, /*bMarkRenderStateDirty=*/false);
			}
			else
			{
				ISM->AddInstance(InstanceTransform, false);
			}
		}

		for (UHierarchicalInstancedStaticMeshComponent* ISM : TransferredFoliageISMs)
		{
			if (ISM)
			{
				ISM->MarkRenderStateDirty();
			}
		}
	}

	bTransferredFoliageDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;
}

void ATerraDyneChunk::SetTransferredActorFoliageData(
	const TArray<FString>& InActorClassPaths,
	const TArray<uint8>& InAttachFlags,
	const TArray<int32>& InDefinitionIndices,
	const TArray<FTransform>& InLocalTransforms,
	const TArray<float>& InTerrainOffsets)
{
	TransferredActorFoliageClassPaths = InActorClassPaths;
	TransferredActorFoliageAttachFlags = InAttachFlags;
	TransferredActorFoliageDefinitionIndices = InDefinitionIndices;
	TransferredActorFoliageInstanceLocalTransforms = InLocalTransforms;
	TransferredActorFoliageInstanceTerrainOffsets = InTerrainOffsets;

	const int32 SharedInstanceCount = FMath::Min(
		TransferredActorFoliageDefinitionIndices.Num(),
		TransferredActorFoliageInstanceLocalTransforms.Num());
	if (SharedInstanceCount != TransferredActorFoliageDefinitionIndices.Num() ||
		SharedInstanceCount != TransferredActorFoliageInstanceLocalTransforms.Num())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: Trimming transferred actor foliage data to %d shared instances."),
			GridCoordinate.X, GridCoordinate.Y, SharedInstanceCount);
		TransferredActorFoliageDefinitionIndices.SetNum(SharedInstanceCount);
		TransferredActorFoliageInstanceLocalTransforms.SetNum(SharedInstanceCount);
	}

	if (TransferredActorFoliageInstanceTerrainOffsets.Num() != SharedInstanceCount)
	{
		TransferredActorFoliageInstanceTerrainOffsets.SetNumZeroed(SharedInstanceCount);
	}

	ClearTransferredActorFoliageActors();
	RefreshTransferredActorFoliagePlacement(bTransferredFoliageFollowsTerrain);
}

void ATerraDyneChunk::RefreshTransferredActorFoliagePlacement(bool bSnapToTerrain)
{
	const int32 DefinitionCount = TransferredActorFoliageClassPaths.Num();
	const int32 InstanceCount = TransferredActorFoliageInstanceLocalTransforms.Num();
	if (DefinitionCount == 0 || InstanceCount == 0)
	{
		ClearTransferredActorFoliageActors();
		bTransferredFoliageDirty = false;
		TransferredFoliageDebounceTimer = 0.0f;
		return;
	}

	if (TransferredActorFoliageActors.Num() != InstanceCount)
	{
		ClearTransferredActorFoliageActors();
		TransferredActorFoliageActors.SetNum(InstanceCount);
	}

	const FTransform ChunkTransform = GetActorTransform();
	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
	{
		const int32 DefinitionIndex = TransferredActorFoliageDefinitionIndices.IsValidIndex(InstanceIndex)
			? TransferredActorFoliageDefinitionIndices[InstanceIndex]
			: INDEX_NONE;
		if (!TransferredActorFoliageClassPaths.IsValidIndex(DefinitionIndex))
		{
			continue;
		}

		UClass* ActorClass = LoadClass<AActor>(nullptr, *TransferredActorFoliageClassPaths[DefinitionIndex]);
		if (!ActorClass)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Failed to load transferred actor foliage class '%s'."),
				GridCoordinate.X, GridCoordinate.Y, *TransferredActorFoliageClassPaths[DefinitionIndex]);
			continue;
		}

		const AActor* ActorCDO = ActorClass->GetDefaultObject<AActor>();
		const bool bSpawnLocally = HasAuthority() || !(ActorCDO && ActorCDO->GetIsReplicated());
		AActor* ExistingActor = TransferredActorFoliageActors[InstanceIndex];
		if (!bSpawnLocally)
		{
			if (ExistingActor)
			{
				ExistingActor->Destroy();
				TransferredActorFoliageActors[InstanceIndex] = nullptr;
			}
			continue;
		}

		FTransform LocalTransform = TransferredActorFoliageInstanceLocalTransforms[InstanceIndex];
		if (bSnapToTerrain)
		{
			FVector LocalLocation = LocalTransform.GetLocation();
			const float TerrainOffset = TransferredActorFoliageInstanceTerrainOffsets.IsValidIndex(InstanceIndex)
				? TransferredActorFoliageInstanceTerrainOffsets[InstanceIndex]
				: 0.0f;
			LocalLocation.Z = GetHeightAtLocation(FVector(LocalLocation.X, LocalLocation.Y, 0.0f)) + TerrainOffset;
			LocalTransform.SetLocation(LocalLocation);
		}

		const bool bAttachToChunk =
			TransferredActorFoliageAttachFlags.IsValidIndex(DefinitionIndex) &&
			TransferredActorFoliageAttachFlags[DefinitionIndex] != 0;
		const FTransform WorldTransform = LocalTransform * ChunkTransform;

		if (!ExistingActor || ExistingActor->GetClass() != ActorClass || ExistingActor->IsActorBeingDestroyed())
		{
			if (ExistingActor)
			{
				ExistingActor->Destroy();
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;

			ExistingActor = GetWorld() ? GetWorld()->SpawnActor<AActor>(ActorClass, WorldTransform, SpawnParams) : nullptr;
			TransferredActorFoliageActors[InstanceIndex] = ExistingActor;
		}

		if (!ExistingActor)
		{
			continue;
		}

		// Skip repositioning entirely when the target transform hasn't actually changed since last
		// refresh (the common case for most instances on a localized terrain edit): SetActorTransform /
		// AttachToComponent trigger overlap and physics re-evaluation, which is wasted work when this
		// tree wasn't near the edited area and its snapped height came out identical.
		const bool bAlreadyAttached = ExistingActor->GetAttachParentActor() == this;
		const bool bWantsAttach = bAttachToChunk;
		const bool bTransformUnchanged = (bWantsAttach == bAlreadyAttached) &&
			(bWantsAttach
				? ExistingActor->GetRootComponent() && ExistingActor->GetRootComponent()->GetRelativeTransform().Equals(LocalTransform, 0.01f)
				: ExistingActor->GetActorTransform().Equals(WorldTransform, 0.01f));
		if (bTransformUnchanged)
		{
			continue;
		}

		if (bAttachToChunk && ExistingActor->GetRootComponent() && GetRootComponent())
		{
			ExistingActor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			ExistingActor->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			ExistingActor->SetActorRelativeTransform(LocalTransform);
		}
		else
		{
			if (bAlreadyAttached)
			{
				ExistingActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			}
			ExistingActor->SetActorTransform(WorldTransform);
		}
	}

	bTransferredFoliageDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;
}

bool ATerraDyneChunk::GetTransferredActorFoliageInstanceTransform(int32 InstanceIndex, FTransform& OutTransform) const
{
	if (!TransferredActorFoliageActors.IsValidIndex(InstanceIndex) || !TransferredActorFoliageActors[InstanceIndex])
	{
		return false;
	}

	OutTransform = TransferredActorFoliageActors[InstanceIndex]->GetActorTransform();
	return true;
}

void ATerraDyneChunk::RequestTransferredFoliageRefresh()
{
	if (!bTransferredFoliageFollowsTerrain ||
		(TransferredFoliageInstanceLocalTransforms.Num() == 0 &&
		 TransferredActorFoliageInstanceLocalTransforms.Num() == 0))
	{
		return;
	}

	float RefreshDelay = 0.5f;
	if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
	{
		RefreshDelay = Settings->FoliageDebounceTime;
	}

	bTransferredFoliageDirty = true;
	TransferredFoliageDebounceTimer = RefreshDelay;
}

void ATerraDyneChunk::SetTransferredFoliageFollowsTerrain(bool bInFollowsTerrain)
{
	bTransferredFoliageFollowsTerrain = bInFollowsTerrain;
	if (bTransferredFoliageFollowsTerrain)
	{
		RequestTransferredFoliageRefresh();
	}
}

void ATerraDyneChunk::ClearTransferredFoliageComponents()
{
	for (UHierarchicalInstancedStaticMeshComponent* ISM : TransferredFoliageISMs)
	{
		if (ISM)
		{
			ISM->DestroyComponent();
		}
	}

	TransferredFoliageISMs.Reset();
}

void ATerraDyneChunk::ClearTransferredActorFoliageActors()
{
	for (AActor* Actor : TransferredActorFoliageActors)
	{
		if (Actor && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}

	TransferredActorFoliageActors.Reset();
}
