// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneGeoTIFFReader.h"

#include "TerraDyneModule.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

// GDAL's C API. Plain C, safe to include from C++ directly -- no wrapper header needed.
#include "gdal.h"
#include "ogr_srs_api.h"
#include "cpl_string.h"
#include "cpl_conv.h"

FVector2D FTerraDyneGeoTIFFRaster::PixelToCrs(double Col, double Row) const
{
	return FVector2D(A0 + Col * A1 + Row * A2, B0 + Col * B1 + Row * B2);
}

FVector2D FTerraDyneGeoTIFFRaster::CrsToPixel(double CrsX, double CrsY) const
{
	const double DX = CrsX - A0;
	const double DY = CrsY - B0;
	return FVector2D(DX * InvA1 + DY * InvA2, DX * InvB1 + DY * InvB2);
}

namespace
{
	/**
	 * GDALAllRegister() populates the driver registry (GTiff, and every other format GDAL was built
	 * with) -- has to run once before GDALOpen can recognise anything. Idempotent on GDAL's side,
	 * but there is no reason to pay the registry-walk cost more than once per process.
	 */
	void EnsureGdalRegistered()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}
		bRegistered = true;

		// PROJ and GDAL both ship runtime resource files (PROJ's coordinate-transform database,
		// GDAL's own CSV/JSON reference data) that GDAL.Build.cs copies into this plugin's own
		// Binaries/Win64/GDALResources -- without pointing PROJ/GDAL at them explicitly, PROJ logs
		// "Cannot find proj.db" and silently falls back to reduced-accuracy transforms.
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TerraDyne")))
		{
			const FString ResourcesDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("GDALResources"));
			const FString ProjDir = FPaths::Combine(ResourcesDir, TEXT("proj"));
			const FString GdalDir = FPaths::Combine(ResourcesDir, TEXT("gdal"));

			if (FPaths::DirectoryExists(ProjDir))
			{
				CPLSetConfigOption("PROJ_DATA", TCHAR_TO_UTF8(*ProjDir));
			}
			if (FPaths::DirectoryExists(GdalDir))
			{
				CPLSetConfigOption("GDAL_DATA", TCHAR_TO_UTF8(*GdalDir));
			}
		}

		GDALAllRegister();
	}

	/** EPSG code of a spatial reference's PROJCS/GEOGCS authority, or 0 if it has none. */
	int32 GetAuthorityCodeOrZero(OGRSpatialReferenceH SRS)
	{
		const char* Code = OSRGetAuthorityCode(SRS, nullptr);
		return Code ? FCString::Atoi(UTF8_TO_TCHAR(Code)) : 0;
	}
}

bool FTerraDyneRasterInfo::IsCompatibleWith(const FTerraDyneRasterInfo& Other, FString& OutReason) const
{
	OutReason.Reset();

	if (Width != Other.Width || Height != Other.Height)
	{
		OutReason = FString::Printf(TEXT("pixel dimensions differ (%dx%d vs %dx%d)"),
			Width, Height, Other.Width, Other.Height);
		return false;
	}

	// Scale/rotation must match to a millionth of a pixel -- these are written from the same source
	// transform for a genuinely matched set, so any real difference means a different grid.
	const double ScaleTolerance = FMath::Max(PixelSizeCrs(), Other.PixelSizeCrs()) * 1.0e-6;
	auto NearlySame = [ScaleTolerance](double A, double B)
	{
		return FMath::Abs(A - B) <= ScaleTolerance;
	};

	if (!NearlySame(A1, Other.A1) || !NearlySame(B2, Other.B2)
		|| !NearlySame(A2, Other.A2) || !NearlySame(B1, Other.B1))
	{
		OutReason = FString::Printf(TEXT("pixel scale/rotation differs (%.10g,%.10g vs %.10g,%.10g)"),
			A1, B2, Other.A1, Other.B2);
		return false;
	}

	if (bIsGeographicWGS84 != Other.bIsGeographicWGS84 || bIsUTM != Other.bIsUTM
		|| (bIsUTM && (UTMZone != Other.UTMZone || bUTMNorth != Other.bUTMNorth)))
	{
		OutReason = TEXT("coordinate reference systems differ");
		return false;
	}

	// Origin: allow up to half a pixel plus a whisker of float slop. See the header for why half a
	// pixel specifically -- it is the RasterPixelIsPoint/RasterPixelIsArea difference, not a mismatch.
	const double OriginTolerance = FMath::Max(PixelSizeCrs(), Other.PixelSizeCrs()) * 0.5 + ScaleTolerance;
	if (FMath::Abs(A0 - Other.A0) > OriginTolerance || FMath::Abs(B0 - Other.B0) > OriginTolerance)
	{
		const double OffsetPixels = FMath::Max(
			FMath::Abs(A0 - Other.A0), FMath::Abs(B0 - Other.B0)) / FMath::Max(PixelSizeCrs(), UE_DOUBLE_SMALL_NUMBER);
		OutReason = FString::Printf(
			TEXT("origins are %.2f pixels apart, which is more than the half-pixel grid-convention allowance -- this looks like a different area"),
			OffsetPixels);
		return false;
	}

	return true;
}

bool FTerraDyneGeoTIFFReader::ProbeMetadata(const FString& FilePath, FTerraDyneRasterInfo& OutInfo, FString& OutError)
{
	OutInfo = FTerraDyneRasterInfo();
	OutError.Reset();

	EnsureGdalRegistered();

	GDALDatasetH Dataset = GDALOpen(TCHAR_TO_UTF8(*FilePath), GA_ReadOnly);
	if (!Dataset)
	{
		OutError = FString::Printf(TEXT("GDAL could not open '%s'."), *FilePath);
		return false;
	}
	ON_SCOPE_EXIT{ GDALClose(Dataset); };

	OutInfo.Width = GDALGetRasterXSize(Dataset);
	OutInfo.Height = GDALGetRasterYSize(Dataset);
	OutInfo.BandCount = GDALGetRasterCount(Dataset);

	if (OutInfo.Width <= 0 || OutInfo.Height <= 0)
	{
		OutError = TEXT("GDAL reports a zero or negative image size.");
		return false;
	}
	if (OutInfo.BandCount <= 0)
	{
		OutError = TEXT("This file has no raster bands.");
		return false;
	}

	double GeoTransform[6];
	if (GDALGetGeoTransform(Dataset, GeoTransform) != CE_None)
	{
		OutError = TEXT("This file has no affine transform (GeoTransform) -- it cannot be georeferenced.");
		return false;
	}
	OutInfo.A0 = GeoTransform[0];
	OutInfo.A1 = GeoTransform[1];
	OutInfo.A2 = GeoTransform[2];
	OutInfo.B0 = GeoTransform[3];
	OutInfo.B1 = GeoTransform[4];
	OutInfo.B2 = GeoTransform[5];

	if (FMath::IsNearlyZero(OutInfo.A1 * OutInfo.B2 - OutInfo.A2 * OutInfo.B1))
	{
		OutError = TEXT("This file's affine transform is singular (degenerate pixel scale).");
		return false;
	}

	// AREA_OR_POINT is what shifts a DEM's reported origin by half a pixel relative to products
	// derived from it; recorded so the mismatch can be explained rather than just tolerated.
	if (const char* AreaOrPoint = GDALGetMetadataItem(Dataset, "AREA_OR_POINT", nullptr))
	{
		OutInfo.bPixelIsPoint = (FCStringAnsi::Stricmp(AreaOrPoint, "Point") == 0);
	}

	const char* Wkt = GDALGetProjectionRef(Dataset);
	if (!Wkt || Wkt[0] == '\0')
	{
		OutError = TEXT("This file has no coordinate system -- it cannot be georeferenced.");
		return false;
	}

	OGRSpatialReferenceH SRS = OSRNewSpatialReference(nullptr);
	ON_SCOPE_EXIT{ OSRDestroySpatialReference(SRS); };

	char* WktCursor = const_cast<char*>(Wkt);
	if (OSRImportFromWkt(SRS, &WktCursor) != OGRERR_NONE)
	{
		OutError = TEXT("GDAL could not parse this file's coordinate system.");
		return false;
	}

	if (OSRIsGeographic(SRS))
	{
		const int32 EpsgCode = GetAuthorityCodeOrZero(SRS);
		if (EpsgCode != 4326 && EpsgCode != 0)
		{
			OutError = FString::Printf(TEXT("Unsupported geographic datum (EPSG:%d) -- only WGS84 (EPSG:4326) is supported."), EpsgCode);
			return false;
		}
		OutInfo.bIsGeographicWGS84 = true;
	}
	else if (OSRIsProjected(SRS))
	{
		const int32 EpsgCode = GetAuthorityCodeOrZero(SRS);
		if (EpsgCode >= 32601 && EpsgCode <= 32660)
		{
			OutInfo.bIsUTM = true;
			OutInfo.UTMZone = EpsgCode - 32600;
			OutInfo.bUTMNorth = true;
		}
		else if (EpsgCode >= 32701 && EpsgCode <= 32760)
		{
			OutInfo.bIsUTM = true;
			OutInfo.UTMZone = EpsgCode - 32700;
			OutInfo.bUTMNorth = false;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported projected coordinate system (EPSG:%d) -- only standard WGS84 UTM zones are supported."), EpsgCode);
			return false;
		}
	}
	else
	{
		OutError = TEXT("Unsupported coordinate system type -- expected Geographic or Projected.");
		return false;
	}

	return true;
}

bool FTerraDyneGeoTIFFReader::LoadFromFile(const FString& FilePath, FTerraDyneGeoTIFFRaster& OutRaster, FString& OutError)
{
	OutRaster = FTerraDyneGeoTIFFRaster();
	OutError.Reset();

	EnsureGdalRegistered();

	GDALDatasetH Dataset = GDALOpen(TCHAR_TO_UTF8(*FilePath), GA_ReadOnly);
	if (!Dataset)
	{
		OutError = FString::Printf(TEXT("GDAL could not open '%s' (see the log above for GDAL's own error)."), *FilePath);
		return false;
	}
	ON_SCOPE_EXIT{ GDALClose(Dataset); };

	const int32 Width = GDALGetRasterXSize(Dataset);
	const int32 Height = GDALGetRasterYSize(Dataset);
	if (Width <= 0 || Height <= 0)
	{
		OutError = TEXT("GDAL reports a zero or negative image size.");
		return false;
	}

	const int32 BandCount = GDALGetRasterCount(Dataset);
	if (BandCount != 1)
	{
		OutError = FString::Printf(TEXT("Only single-band elevation GeoTIFFs are supported; this file has %d band(s)."), BandCount);
		return false;
	}

	GDALRasterBandH Band = GDALGetRasterBand(Dataset, 1);
	if (!Band)
	{
		OutError = TEXT("GDAL could not access the raster band.");
		return false;
	}

	OutRaster.Width = Width;
	OutRaster.Height = Height;
	OutRaster.ElevationMeters.SetNumUninitialized(Width * Height);

	// GDT_Float32 asks GDAL to convert whatever the file's native sample type is (UInt8/UInt16/
	// Int16/Float32/Float64/...) to float on the way out -- this is exactly the sample-format
	// handling a hand-rolled reader would otherwise have to special-case per type.
	const CPLErr ReadErr = GDALRasterIO(
		Band, GF_Read,
		0, 0, Width, Height,
		OutRaster.ElevationMeters.GetData(), Width, Height,
		GDT_Float32, 0, 0);

	if (ReadErr != CE_None)
	{
		OutError = TEXT("GDAL failed to read the raster band's pixel data.");
		return false;
	}

	// NoData is read from the band and EXCLUDED from the elevation range.
	//
	// The sample values themselves are deliberately left untouched -- consumers still see the raw
	// sentinel and decide per-sample what to do with it (hole, clamp, interpolate) -- but letting a
	// -9999 fill into Min/Max would wreck every downstream [0,1] normalisation built on this range:
	// real terrain would occupy a fraction of a percent at the top of the band and render as a flat
	// smear. A DEM with a NoData border is the common case, not an exotic one.
	int32 NoDataSuccess = 0;
	const double BandNoData = GDALGetRasterNoDataValue(Band, &NoDataSuccess);
	OutRaster.bHasNoData = (NoDataSuccess != 0);
	OutRaster.NoDataValue = OutRaster.bHasNoData ? BandNoData : 0.0;

	double MinZ = TNumericLimits<double>::Max();
	double MaxZ = TNumericLimits<double>::Lowest();
	bool bAnyValid = false;
	int64 NoDataCount = 0;
	for (const float Z : OutRaster.ElevationMeters)
	{
		if (!OutRaster.IsValidSample(Z))
		{
			++NoDataCount;
			continue;
		}
		bAnyValid = true;
		MinZ = FMath::Min(MinZ, (double)Z);
		MaxZ = FMath::Max(MaxZ, (double)Z);
	}

	OutRaster.bHasAnyValidSample = bAnyValid;
	if (!bAnyValid)
	{
		// Every cell is NoData. Better to fail loudly here than to hand back a raster whose range is
		// [DBL_MAX, -DBL_MAX] and let it produce nonsense terrain far downstream.
		OutError = TEXT("Every sample in this raster is NoData -- there is no elevation data to import.");
		return false;
	}

	OutRaster.ElevationMinMeters = MinZ;
	OutRaster.ElevationMaxMeters = MaxZ;

	// Affine pixel->CRS transform: GDAL's GeoTransform is [OriginX, PixelW, RowRot, OriginY,
	// ColRot, PixelH], which is exactly FTerraDyneGeoTIFFRaster's (A0,A1,A2,B0,B1,B2) already.
	double GeoTransform[6];
	if (GDALGetGeoTransform(Dataset, GeoTransform) != CE_None)
	{
		OutError = TEXT("GeoTIFF has no affine transform (GeoTransform) -- cannot georeference it.");
		return false;
	}
	OutRaster.A0 = GeoTransform[0];
	OutRaster.A1 = GeoTransform[1];
	OutRaster.A2 = GeoTransform[2];
	OutRaster.B0 = GeoTransform[3];
	OutRaster.B1 = GeoTransform[4];
	OutRaster.B2 = GeoTransform[5];

	const double Det = OutRaster.A1 * OutRaster.B2 - OutRaster.A2 * OutRaster.B1;
	if (FMath::IsNearlyZero(Det))
	{
		OutError = TEXT("GeoTIFF's affine transform is singular (degenerate pixel scale).");
		return false;
	}
	OutRaster.InvA1 = OutRaster.B2 / Det;
	OutRaster.InvA2 = -OutRaster.A2 / Det;
	OutRaster.InvB1 = -OutRaster.B1 / Det;
	OutRaster.InvB2 = OutRaster.A1 / Det;

	// CRS classification via GDAL's spatial reference API, narrowed to what
	// FTerraDyneGeoTIFFHeightSource's Cesium bridge actually implements: geographic WGS84, or a
	// standard WGS84 UTM zone (EPSG 326xx north / 327xx south).
	const char* Wkt = GDALGetProjectionRef(Dataset);
	if (!Wkt || Wkt[0] == '\0')
	{
		OutError = TEXT("GeoTIFF has no coordinate system -- cannot georeference it.");
		return false;
	}

	OGRSpatialReferenceH SRS = OSRNewSpatialReference(nullptr);
	ON_SCOPE_EXIT{ OSRDestroySpatialReference(SRS); };

	// OSRImportFromWkt takes char** and advances the caller's pointer past what it consumed --
	// operates on a local copy of the pointer, never on GDAL's own Wkt string.
	char* WktCursor = const_cast<char*>(Wkt);
	if (OSRImportFromWkt(SRS, &WktCursor) != OGRERR_NONE)
	{
		OutError = TEXT("GDAL could not parse the GeoTIFF's coordinate system.");
		return false;
	}

	if (OSRIsGeographic(SRS))
	{
		const int32 EpsgCode = GetAuthorityCodeOrZero(SRS);
		if (EpsgCode != 4326 && EpsgCode != 0)
		{
			OutError = FString::Printf(TEXT("Unsupported geographic datum (EPSG:%d) -- only WGS84 (EPSG:4326) is supported."), EpsgCode);
			return false;
		}
		OutRaster.bIsGeographicWGS84 = true;
	}
	else if (OSRIsProjected(SRS))
	{
		const int32 EpsgCode = GetAuthorityCodeOrZero(SRS);
		// Standard WGS84 UTM EPSG numbering: 326xx = north zone xx, 327xx = south zone xx.
		if (EpsgCode >= 32601 && EpsgCode <= 32660)
		{
			OutRaster.bIsUTM = true;
			OutRaster.UTMZone = EpsgCode - 32600;
			OutRaster.bUTMNorth = true;
		}
		else if (EpsgCode >= 32701 && EpsgCode <= 32760)
		{
			OutRaster.bIsUTM = true;
			OutRaster.UTMZone = EpsgCode - 32700;
			OutRaster.bUTMNorth = false;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported projected coordinate system (EPSG:%d) -- only standard WGS84 UTM zones are supported."), EpsgCode);
			return false;
		}
	}
	else
	{
		OutError = TEXT("Unsupported or missing coordinate system type -- expected Geographic or Projected.");
		return false;
	}

	UE_LOG(LogTerraDyne, Log, TEXT("GeoTIFF loaded via GDAL: %dx%d, elevation [%.1f, %.1f]m over valid samples (%lld NoData cell(s)%s), %s"),
		Width, Height, MinZ, MaxZ, NoDataCount,
		OutRaster.bHasNoData ? *FString::Printf(TEXT(", sentinel %.1f"), OutRaster.NoDataValue) : TEXT(", none declared"),
		OutRaster.bIsGeographicWGS84 ? TEXT("WGS84 geographic")
			: (OutRaster.bIsUTM ? *FString::Printf(TEXT("UTM zone %d%s"), OutRaster.UTMZone, OutRaster.bUTMNorth ? TEXT("N") : TEXT("S")) : TEXT("unknown CRS")));

	return true;
}
