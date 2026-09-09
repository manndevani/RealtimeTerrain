// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "GRTTileData.generated.h"

UENUM(BlueprintType)
enum class EGRTLayer : uint8
{
	Base    UMETA(DisplayName = "Base"),
	Sculpt  UMETA(DisplayName = "Sculpt"),
	Detail  UMETA(DisplayName = "Detail"),
	Active  UMETA(DisplayName = "Active Selection")
};

UENUM(BlueprintType)
enum class EGRTBrushMode : uint8
{
	Raise   UMETA(DisplayName = "Raise"),
	Lower   UMETA(DisplayName = "Lower"),
	Flatten UMETA(DisplayName = "Flatten"),
	Smooth  UMETA(DisplayName = "Smooth")
};

/** Parameters for a single brush application, sent over the network. */
USTRUCT(BlueprintType)
struct FGRTBrushParams
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize WorldLocation = FVector::ZeroVector;

	UPROPERTY()
	float Radius = 2000.f;

	UPROPERTY()
	float Strength = 1.f;

	UPROPERTY()
	EGRTBrushMode BrushMode = EGRTBrushMode::Raise;

	UPROPERTY()
	float FlattenHeight = 0.f;

	UPROPERTY()
	bool bIsStrokeStart = false;
};

/**
 * UGRTTileData
 * 
 * A static data asset containing baked terrain information for a specific chunk.
 * 
 * Workflow:
 * 1. Editor: Use UGRTBaker to convert Landscape Components into these assets.
 * 2. Disk: Stored as compressed .uasset files.
 * 3. Runtime: Streamed in by AGRTChunk::InitializeFromAsset().
 */
UCLASS(BlueprintType)
class GRT_API UGRTTileData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGRTTileData();

	//--- Data Payloads ---//

	/** 
	 * Quantized Height Data (16-bit Unsigned).
	 * Range: 0 (MinHeight) to 65535 (MaxHeight).
	 * This array is usually (Resolution * Resolution) in length.
	 * Using uint16 reduces memory footprint by 50% compared to float.
	 */
	 // Blueprints cannot read uint16 directly. We rely on C++ accessors if needed.
	UPROPERTY(VisibleAnywhere, Category = "Baked Data")
	TArray<uint16> InitialHeightMap;

	//--- Metadata ---//

	/** The vertex resolution of the tile (e.g., 128, 256). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dimensions")
	int32 Resolution;

	/** The physical size of this tile in Unreal Units (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dimensions")
	float RealWorldSize;

	//--- Scaling Info ---//

	/** 
	 * The Z-Scale multiplier used during baking.
	 * Used to de-quantize the uint16 data back to World Z floats.
	 * Standard calculation: Height(float) = (Data / 65535.0f) * ZScale * 512.0f;
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scaling")
	float BakedZScale;

	/** 
	 * World Coordinate identifier (optional, mostly for debugging). 
	 * Helps identify which grid slot this asset belongs to.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FIntPoint GridCoordinate;

#if WITH_EDITORONLY_DATA
	/** Source Landscape Component name (for re-baking traceability). */
	UPROPERTY(VisibleAnywhere, Category = "Source Info")
	FString SourceComponentName;
#endif

public:
	/**
	 * Helper to get the number of bytes this asset consumes in memory.
	 */
	UFUNCTION(BlueprintPure, Category = "GRT")
	int32 GetMemoryFootprint() const;
};
