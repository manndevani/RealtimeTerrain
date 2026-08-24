// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneRasterLayers.h"
#include "IO/TerraDyneGeoTIFFReader.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "TerraDyneRasterLayers"

namespace TerraDyneRasterLayers
{

FText GetDisplayName(ETerraDyneRasterLayer Layer)
{
	switch (Layer)
	{
	case ETerraDyneRasterLayer::Elevation:      return LOCTEXT("Layer_Elevation", "Elevation (DEM)");
	case ETerraDyneRasterLayer::Hillshade:      return LOCTEXT("Layer_Hillshade", "Hillshade");
	case ETerraDyneRasterLayer::ColorRelief:    return LOCTEXT("Layer_ColorRelief", "Color Relief");
	case ETerraDyneRasterLayer::ColorHillshade: return LOCTEXT("Layer_ColorHillshade", "Color Hillshade");
	case ETerraDyneRasterLayer::Slope:          return LOCTEXT("Layer_Slope", "Slope");
	case ETerraDyneRasterLayer::Aspect:         return LOCTEXT("Layer_Aspect", "Aspect");
	case ETerraDyneRasterLayer::Roughness:      return LOCTEXT("Layer_Roughness", "Roughness");
	case ETerraDyneRasterLayer::Satellite:      return LOCTEXT("Layer_Satellite", "Satellite / Ortho");
	default:                                    return LOCTEXT("Layer_Unknown", "Unknown");
	}
}

bool IsBandCountValidFor(ETerraDyneRasterLayer Layer, int32 BandCount, FString& OutReason)
{
	OutReason.Reset();

	if (BandCount <= 0)
	{
		OutReason = TEXT("the file has no raster bands");
		return false;
	}

	if (IsElevation(Layer))
	{
		if (BandCount != 1)
		{
			// Worth being explicit: pointing a DEM slot at a colour product is an easy mistake, and
			// "expected 1 band, got 3" on its own reads like a file problem rather than a mix-up.
			OutReason = FString::Printf(
				TEXT("elevation must be a single-band raster, but this file has %d bands -- it looks like a colour image (hillshade/relief) rather than a DEM"),
				BandCount);
			return false;
		}
		return true;
	}

	// Overlays: supports single-band greyscale, RGB, RGBA, and multi-spectral satellite products (e.g. Sentinel-2 / Landsat / WorldView)
	return true;
}

bool AutoDetectRasterType(
	const FString& FilePath,
	ETerraDyneRasterLayer& OutType,
	FString& OutTypeLabel,
	FTerraDyneRasterInfo& OutInfo,
	FString& OutError)
{
	if (!FTerraDyneGeoTIFFReader::ProbeMetadata(FilePath, OutInfo, OutError))
	{
		return false;
	}

	const FString LowerName = FPaths::GetCleanFilename(FilePath).ToLower();

	if (OutInfo.BandCount == 1)
	{
		if (LowerName.Contains(TEXT("slope")))
		{
			OutType = ETerraDyneRasterLayer::Slope;
			OutTypeLabel = TEXT("Slope");
		}
		else if (LowerName.Contains(TEXT("aspect")))
		{
			OutType = ETerraDyneRasterLayer::Aspect;
			OutTypeLabel = TEXT("Aspect");
		}
		else if (LowerName.Contains(TEXT("roughness")) || LowerName.Contains(TEXT("tri")) || LowerName.Contains(TEXT("tpi")))
		{
			OutType = ETerraDyneRasterLayer::Roughness;
			OutTypeLabel = TEXT("Roughness");
		}
		else if (LowerName.Contains(TEXT("hillshade")) || LowerName.Contains(TEXT("shade")))
		{
			OutType = ETerraDyneRasterLayer::Hillshade;
			OutTypeLabel = TEXT("Hillshade");
		}
		else if (LowerName.Contains(TEXT("dem")) || LowerName.Contains(TEXT("elevation")) || LowerName.Contains(TEXT("dtm")) || LowerName.Contains(TEXT("dsm")))
		{
			OutType = ETerraDyneRasterLayer::Elevation;
			OutTypeLabel = TEXT("Elevation (DEM)");
		}
		else
		{
			OutType = ETerraDyneRasterLayer::Hillshade;
			OutTypeLabel = TEXT("Hillshade / Analytical");
		}
	}
	else if (OutInfo.BandCount >= 3)
	{
		if (LowerName.Contains(TEXT("color-hillshade")) || LowerName.Contains(TEXT("hillshade-color")) || LowerName.Contains(TEXT("color_hillshade")))
		{
			OutType = ETerraDyneRasterLayer::ColorHillshade;
			OutTypeLabel = TEXT("Color Hillshade");
		}
		else if (LowerName.Contains(TEXT("relief")) || LowerName.Contains(TEXT("color-relief")) || LowerName.Contains(TEXT("color_relief")))
		{
			OutType = ETerraDyneRasterLayer::ColorRelief;
			OutTypeLabel = TEXT("Color Relief");
		}
		else if (LowerName.Contains(TEXT("sat")) || LowerName.Contains(TEXT("ortho")) || LowerName.Contains(TEXT("rgb")) || LowerName.Contains(TEXT("truecolor")) || LowerName.Contains(TEXT("sentinel")) || LowerName.Contains(TEXT("landsat")))
		{
			OutType = ETerraDyneRasterLayer::Satellite;
			OutTypeLabel = TEXT("Satellite / Ortho");
		}
		else
		{
			OutType = ETerraDyneRasterLayer::Satellite;
			OutTypeLabel = TEXT("Satellite / Ortho (Multi-Band)");
		}
	}
	else
	{
		OutType = ETerraDyneRasterLayer::Satellite;
		OutTypeLabel = TEXT("Multi-Band Composite");
	}

	return true;
}

} // namespace TerraDyneRasterLayers

#undef LOCTEXT_NAMESPACE
