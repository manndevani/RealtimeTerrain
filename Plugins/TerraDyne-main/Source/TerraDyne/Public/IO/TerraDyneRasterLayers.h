// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IO/TerraDyneGeoTIFFReader.h"
#include "TerraDyneRasterLayers.generated.h"

/**
 * The kinds of raster a TerraDyne world can be built from.
 *
 * Elevation is the only one that shapes the terrain; everything else describes the SAME ground and
 * is draped over it. That asymmetry is the whole reason this enum exists rather than a flat list:
 * the elevation raster establishes the reference grid that every other layer is checked against, so
 * it has to be imported first and cannot be swapped out from under the others.
 *
 * The derived names match what gdaldem emits, since a matched set is almost always produced by
 * running gdaldem over one source DEM.
 */
UENUM(BlueprintType)
enum class ETerraDyneRasterLayer : uint8
{
	/** The DEM. Required, imported first, and the grid every other layer must agree with. */
	Elevation       UMETA(DisplayName = "Elevation (DEM)"),

	Hillshade       UMETA(DisplayName = "Hillshade"),
	ColorRelief     UMETA(DisplayName = "Color Relief"),
	ColorHillshade  UMETA(DisplayName = "Color Hillshade"),
	Slope           UMETA(DisplayName = "Slope"),
	Aspect          UMETA(DisplayName = "Aspect"),
	Roughness       UMETA(DisplayName = "Roughness"),

	MAX             UMETA(Hidden)
};

/** One layer slot: whether it is filled, by what file, and on what grid. */
USTRUCT(BlueprintType)
struct TERRADYNE_API FTerraDyneRasterLayerSlot
{
	GENERATED_BODY()

	/** True once a file has been accepted into this slot (header validated, pixels not necessarily read). */
	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Raster")
	bool bImported = false;

	/** True once the layer's pixel data has actually been decoded and applied to the scene. */
	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Raster")
	bool bLoadedIntoScene = false;

	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Raster")
	FString SourceFilePath;

	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Raster")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Raster")
	int32 Height = 0;

	UPROPERTY(BlueprintReadOnly, Category = "TerraDyne|Raster")
	int32 BandCount = 0;

	/** Not a UPROPERTY: plain C++ georeferencing detail the UI never needs to see field by field. */
	FTerraDyneRasterInfo Info;

	void Reset()
	{
		*this = FTerraDyneRasterLayerSlot();
	}
};

namespace TerraDyneRasterLayers
{
	/** Human-readable name for a layer, for UI and log messages. */
	TERRADYNE_API FText GetDisplayName(ETerraDyneRasterLayer Layer);

	/** True for the one layer that defines the terrain surface, rather than decorating it. */
	FORCEINLINE bool IsElevation(ETerraDyneRasterLayer Layer)
	{
		return Layer == ETerraDyneRasterLayer::Elevation;
	}

	/**
	 * Whether a raster's band count suits the layer it is being imported as.
	 *
	 * Elevation must be single-band -- a 3-band RGB image carries no height. Colour layers are
	 * expected to be 3 or 4 band, greyscale analytical products (slope/aspect/roughness/hillshade)
	 * single-band, but those are accepted either way since a greyscale product saved as RGB is
	 * harmless as an overlay.
	 */
	TERRADYNE_API bool IsBandCountValidFor(ETerraDyneRasterLayer Layer, int32 BandCount, FString& OutReason);
}
