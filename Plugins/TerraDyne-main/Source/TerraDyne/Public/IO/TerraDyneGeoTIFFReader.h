// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * A decoded single-band elevation GeoTIFF: the pixel grid plus enough of its georeferencing to
 * place it in the real world.
 *
 * FTerraDyneGeoTIFFReader::LoadFromFile decodes through GDAL (see Source/ThirdParty/GDAL), so the
 * file format/compression/sample-type support is whatever GDAL's GeoTIFF driver handles -- this
 * struct itself still only carries single-band elevation, and the CRS narrows deliberately to
 * geographic WGS84 or a standard UTM zone (see LoadFromFile's OutError contract): everything
 * downstream (FTerraDyneGeoTIFFHeightSource's Cesium bridge) only implements those two.
 */
struct FTerraDyneGeoTIFFRaster
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

	double ElevationMinMeters = 0.0;
	double ElevationMaxMeters = 0.0;

	/** CRS (col,row), fractional, from pixel (col,row) -- the forward affine. */
	FVector2D PixelToCrs(double Col, double Row) const;

	/** Fractional pixel (col,row) from a CRS-space (x,y) -- the affine's inverse. */
	FVector2D CrsToPixel(double CrsX, double CrsY) const;
};

/** Reads a GeoTIFF elevation raster from disk. No UObject/engine-actor dependency. */
class TERRADYNE_API FTerraDyneGeoTIFFReader
{
public:
	/**
	 * Returns false (with OutError explaining why) if GDAL cannot open the file, it isn't
	 * single-band, or its CRS is neither geographic WGS84 nor a standard UTM zone.
	 */
	static bool LoadFromFile(const FString& FilePath, FTerraDyneGeoTIFFRaster& OutRaster, FString& OutError);
};
