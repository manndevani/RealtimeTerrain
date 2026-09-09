// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IO/GRTRasterLayers.h"

/**
 * Utility for detecting compressed raster archives (.tar.gz, .tgz, .zip, .tar) and extracting
 * them into a cached directory so the enclosed GeoTIFF can be loaded seamlessly.
 */
class GRT_API FGRTArchiveHelper
{
public:
	/** Checks whether the given filename or path is a compressed archive. */
	static bool IsArchiveFile(const FString& FilePath);

	/** Checks whether the given filename has a supported raster or archive extension. */
	static bool HasSupportedExtension(const FString& FilePath);

	/**
	 * Resolves an input file path: if it is a .tif/.tiff, returns it directly. If it is an archive,
	 * extracts it to a cache folder in Saved/ExtractedRasters and locates the corresponding .tif/.tiff.
	 */
	static bool ResolveOrExtractRaster(
		const FString& InputPath,
		EGRTRasterLayer Layer,
		FString& OutResolvedPath,
		FString& OutError);
};
