// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * A decoded single-band elevation GeoTIFF: the pixel grid plus enough of its georeferencing to
 * place it in the real world.
 *
 * Deliberately scoped to the common case for DEM/elevation exports (SRTM, Copernicus GLO-30,
 * USGS 3DEP, and similar) rather than the full GeoTIFF specification: single-band Float32/Int16/
 * UInt16 samples, uncompressed or LZW/Deflate compression, and either geographic WGS84 or a
 * standard UTM zone. Anything outside that fails FTerraDyneGeoTIFFReader::LoadFromFile explicitly
 * rather than silently mis-placing terrain -- see its OutError contract.
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
	 * Returns false (with OutError explaining why) if the file cannot be opened, is not a valid
	 * TIFF, or uses a feature outside the supported scope described on FTerraDyneGeoTIFFRaster --
	 * multi-band/paletted images, unsupported compression (anything but none/LZW/Deflate), or a
	 * CRS that is neither geographic WGS84 nor a standard UTM zone.
	 */
	static bool LoadFromFile(const FString& FilePath, FTerraDyneGeoTIFFRaster& OutRaster, FString& OutError);
};
