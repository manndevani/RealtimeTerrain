// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneRasterLayers.h"

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

	// Overlays: anything from single-band greyscale to RGBA. More than four bands is a multispectral
	// product this system has no way to interpret.
	if (BandCount > 4)
	{
		OutReason = FString::Printf(TEXT("overlay layers support 1 to 4 bands, but this file has %d"), BandCount);
		return false;
	}

	return true;
}

} // namespace TerraDyneRasterLayers

#undef LOCTEXT_NAMESPACE
