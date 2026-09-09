// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * A decoded single-band elevation GeoTIFF: the pixel grid plus enough of its georeferencing to
 * place it in the real world.
 *
 * FGRTGeoTIFFReader::LoadFromFile decodes through GDAL (see Source/ThirdParty/GDAL), so the
 * file format/compression/sample-type support is whatever GDAL's GeoTIFF driver handles -- this
 * struct itself still only carries single-band elevation, and the CRS narrows deliberately to
 * geographic WGS84 or a standard UTM zone (see LoadFromFile's OutError contract): everything
 * downstream (FGRTGeoTIFFHeightSource's Cesium bridge) only implements those two.
 */
struct FGRTGeoTIFFRaster
{
	/** Row-major elevation samples in metres, Width*Height, row 0 = the TIFF's first (top) row. */
	TArray<float> ElevationMeters;

	int32 Width = 0;
	int32 Height = 0;

	/**
	 * Affine pixel -> CRS transform, in the raster's own CRS units (degrees if bIsGeographicWGS84,
	 * metres if bIsUTM): CrsX = A0 + Col*A1 + Row*A2 ; CrsY = B0 + Col*B1 + Row*B2. Pixel (0,0) is
	 * the centre of the top-left sample (GeoTIFF's RasterPixelIsArea convention, the default and
	 * by far the most common raster space type for elevation products).
	 */
	double A0 = 0.0, A1 = 0.0, A2 = 0.0;
	double B0 = 0.0, B1 = 0.0, B2 = 0.0;

	/** Determinant-based inverse of the 2x2 (A1,A2,B1,B2) block, precomputed for CrsToPixel(). */
	double InvA1 = 0.0, InvA2 = 0.0, InvB1 = 0.0, InvB2 = 0.0;

	bool bIsGeographicWGS84 = false;

	bool bIsUTM = false;
	int32 UTMZone = 0;
	bool bUTMNorth = true;

	/**
	 * Elevation range over VALID samples only -- NoData cells are excluded (see bHasNoData).
	 *
	 * This matters far more than it looks: a real DEM's NoData fill is typically a huge sentinel like
	 * -9999, and every consumer normalises elevation into [0,1] against this range. Including the
	 * sentinel would stretch the band so far that all genuine terrain collapses into a sliver at the
	 * very top of it -- a mountain range flattened into noise a few centimetres tall.
	 */
	double ElevationMinMeters = 0.0;
	double ElevationMaxMeters = 0.0;

	/** True when the source band declares a NoData value; NoDataValue is then meaningful. */
	bool bHasNoData = false;

	/** The band's NoData sentinel, verbatim, in the same units as ElevationMeters. */
	double NoDataValue = 0.0;

	/** True if at least one sample was NOT NoData. A fully-empty raster is not usable terrain. */
	bool bHasAnyValidSample = false;

	/** True when this sample is real terrain rather than a NoData fill. */
	FORCEINLINE bool IsValidSample(float Sample) const
	{
		return !bHasNoData || !FMath::IsNearlyEqual((double)Sample, NoDataValue, 1e-6);
	}

	/** CRS (col,row), fractional, from pixel (col,row) -- the forward affine. */
	FVector2D PixelToCrs(double Col, double Row) const;

	/** Fractional pixel (col,row) from a CRS-space (x,y) -- the affine's inverse. */
	FVector2D CrsToPixel(double CrsX, double CrsY) const;
};

/**
 * Everything about a raster that can be known without decoding a single pixel.
 *
 * Exists so a multi-layer import can validate a 600 MB file in milliseconds instead of decoding it:
 * checking that a hillshade lines up with its DEM needs the grid and the CRS, never the samples. The
 * pixel read is deferred until the layer is actually loaded into the scene.
 */
struct FGRTRasterInfo
{
	int32 Width = 0;
	int32 Height = 0;

	/** How many bands the file has: 1 for a DEM or a greyscale product, 3-4 for an RGB(A) product. */
	int32 BandCount = 0;

	/** Affine pixel -> CRS transform, same convention as FGRTGeoTIFFRaster's A/B terms. */
	double A0 = 0.0, A1 = 0.0, A2 = 0.0;
	double B0 = 0.0, B1 = 0.0, B2 = 0.0;

	bool bIsGeographicWGS84 = false;
	bool bIsUTM = false;
	int32 UTMZone = 0;
	bool bUTMNorth = true;

	/** True when the file declares RasterPixelIsPoint rather than the usual RasterPixelIsArea. */
	bool bPixelIsPoint = false;

	/** Longest side of one pixel in CRS units; the natural tolerance scale for grid comparisons. */
	double PixelSizeCrs() const
	{
		return FMath::Max(FMath::Abs(A1), FMath::Abs(B2));
	}

	/**
	 * Whether two rasters describe the same patch of ground on the same grid, so they can be used
	 * together as elevation + overlays.
	 *
	 * Deliberately tolerant about the ORIGIN by half a pixel, because that difference is expected
	 * rather than exceptional: a DEM is commonly tagged RasterPixelIsPoint while the products derived
	 * from it (gdaldem hillshade/slope/aspect/roughness/colour-relief) are written RasterPixelIsArea,
	 * which shifts the reported origin by exactly half a pixel even though every sample lines up.
	 * Requiring an exact match would reject a matched set produced from one source file, which is
	 * the single most common thing a user will try to import.
	 *
	 * Strict about everything that genuinely means "different data": pixel dimensions, pixel scale,
	 * rotation/skew terms, and the coordinate reference system.
	 */
	bool IsCompatibleWith(const FGRTRasterInfo& Other, FString& OutReason) const;
};

/** Reads a GeoTIFF elevation raster from disk. No UObject/engine-actor dependency. */
class GRT_API FGRTGeoTIFFReader
{
public:
	/**
	 * Returns false (with OutError explaining why) if GDAL cannot open the file, it isn't
	 * single-band, or its CRS is neither geographic WGS84 nor a standard UTM zone.
	 */
	static bool LoadFromFile(const FString& FilePath, FGRTGeoTIFFRaster& OutRaster, FString& OutError);

	/**
	 * Reads only the header: size, band count, affine transform and CRS. No pixel data is touched, so
	 * this is effectively instant even on a multi-hundred-megabyte file.
	 *
	 * Unlike LoadFromFile this does NOT require a single band -- an RGB colour-relief or a colour
	 * hillshade is a perfectly valid overlay layer, it is just not elevation. Callers that need
	 * elevation check BandCount themselves.
	 */
	static bool ProbeMetadata(const FString& FilePath, FGRTRasterInfo& OutInfo, FString& OutError);

	/**
	 * Decodes 1-band greyscale, 3-band RGB, or 4-band RGBA imagery into a GPU-ready UTexture2D.
	 * Automatically downsamples large rasters to MaxDimension (default 4096) for fast GPU upload.
	 */
	static class UTexture2D* DecodeRasterToTexture(const FString& FilePath, int32 MaxDimension, FString& OutError);

	/**
	 * Decodes raster imagery (Byte RGB/Greyscale or Float32 Slope/Aspect/Roughness) into an FColor buffer.
	 * Applies scientific GIS colormaps for Float32 analysis layers.
	 */
	static bool DecodeRasterPixels(
		const FString& FilePath,
		int32 MaxDimension,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight,
		FString& OutError);
};
