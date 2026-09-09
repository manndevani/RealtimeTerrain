// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/GRTGeoTIFFReader.h"

#include "GRTModule.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/Texture2D.h"

// GDAL's C API. Plain C, safe to include from C++ directly -- no wrapper header needed.
#include "gdal.h"
#include "ogr_srs_api.h"
#include "cpl_string.h"
#include "cpl_conv.h"

FVector2D FGRTGeoTIFFRaster::PixelToCrs(double Col, double Row) const
{
	return FVector2D(A0 + Col * A1 + Row * A2, B0 + Col * B1 + Row * B2);
}

FVector2D FGRTGeoTIFFRaster::CrsToPixel(double CrsX, double CrsY) const
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
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GRT")))
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

bool FGRTRasterInfo::IsCompatibleWith(const FGRTRasterInfo& Other, FString& OutReason) const
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

bool FGRTGeoTIFFReader::ProbeMetadata(const FString& FilePath, FGRTRasterInfo& OutInfo, FString& OutError)
{
	OutInfo = FGRTRasterInfo();
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

bool FGRTGeoTIFFReader::LoadFromFile(const FString& FilePath, FGRTGeoTIFFRaster& OutRaster, FString& OutError)
{
	OutRaster = FGRTGeoTIFFRaster();
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
	// ColRot, PixelH], which is exactly FGRTGeoTIFFRaster's (A0,A1,A2,B0,B1,B2) already.
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
	// FGRTGeoTIFFHeightSource's Cesium bridge actually implements: geographic WGS84, or a
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

	UE_LOG(LogGRT, Log, TEXT("GeoTIFF loaded via GDAL: %dx%d, elevation [%.1f, %.1f]m over valid samples (%lld NoData cell(s)%s), %s"),
		Width, Height, MinZ, MaxZ, NoDataCount,
		OutRaster.bHasNoData ? *FString::Printf(TEXT(", sentinel %.1f"), OutRaster.NoDataValue) : TEXT(", none declared"),
		OutRaster.bIsGeographicWGS84 ? TEXT("WGS84 geographic")
			: (OutRaster.bIsUTM ? *FString::Printf(TEXT("UTM zone %d%s"), OutRaster.UTMZone, OutRaster.bUTMNorth ? TEXT("N") : TEXT("S")) : TEXT("unknown CRS")));

	return true;
}

namespace
{
	// Slope colormap (degrees: 0 - 90)
	FColor EvaluateSlopeColor(float Degrees)
	{
		Degrees = FMath::Clamp(Degrees, 0.0f, 90.0f);
		if (Degrees < 5.0f) // 0-5 deg: Dark Forest Green (Gentle / Flat)
		{
			const float T = Degrees / 5.0f;
			return FColor(FMath::RoundToInt(FMath::Lerp(15.0f, 40.0f, T)), FMath::RoundToInt(FMath::Lerp(140.0f, 210.0f, T)), 25, 255);
		}
		else if (Degrees < 15.0f) // 5-15 deg: Lime Green / Yellow-Green (Low slope)
		{
			const float T = (Degrees - 5.0f) / 10.0f;
			return FColor(FMath::RoundToInt(FMath::Lerp(40.0f, 190.0f, T)), FMath::RoundToInt(FMath::Lerp(210.0f, 235.0f, T)), 20, 255);
		}
		else if (Degrees < 25.0f) // 15-25 deg: Bright Yellow (Moderate slope)
		{
			const float T = (Degrees - 15.0f) / 10.0f;
			return FColor(FMath::RoundToInt(FMath::Lerp(190.0f, 255.0f, T)), FMath::RoundToInt(FMath::Lerp(235.0f, 180.0f, T)), 15, 255);
		}
		else if (Degrees < 35.0f) // 25-35 deg: Rich Orange (Steep slope)
		{
			const float T = (Degrees - 25.0f) / 10.0f;
			return FColor(255, FMath::RoundToInt(FMath::Lerp(180.0f, 70.0f, T)), 10, 255);
		}
		else // > 35 deg: Crimson Red to Deep Maroon (Cliffs / Precipice)
		{
			const float T = FMath::Clamp((Degrees - 35.0f) / 25.0f, 0.0f, 1.0f);
			return FColor(FMath::RoundToInt(FMath::Lerp(255.0f, 160.0f, T)), FMath::RoundToInt(FMath::Lerp(70.0f, 0.0f, T)), FMath::RoundToInt(FMath::Lerp(10.0f, 20.0f, T)), 255);
		}
	}

	// Aspect colormap (degrees: 0 - 360)
	FColor EvaluateAspectColor(float Degrees)
	{
		const float Normalized = FMath::Fmod(FMath::Max(0.0f, Degrees) + 360.0f, 360.0f);
		const float H = Normalized / 60.0f;
		const int32 Sector = FMath::FloorToInt(H) % 6;
		const float F = H - FMath::FloorToInt(H);
		const float Sat = 0.95f;
		const float Val = 0.95f;
		const float P = Val * (1.0f - Sat);
		const float Q = Val * (1.0f - Sat * F);
		const float T = Val * (1.0f - Sat * (1.0f - F));

		float R = 0.f, G = 0.f, B = 0.f;
		switch (Sector)
		{
		case 0: R = Val; G = T;   B = P;   break;
		case 1: R = Q;   G = Val; B = P;   break;
		case 2: R = P;   G = Val; B = T;   break;
		case 3: R = P;   G = Q;   B = Val; break;
		case 4: R = T;   G = P;   B = Val; break;
		default: R = Val; G = P;   B = Q;   break;
		}

		return FColor(FMath::RoundToInt(R * 255.f), FMath::RoundToInt(G * 255.f), FMath::RoundToInt(B * 255.f), 255);
	}

	// Roughness colormap (normalized 0-1) - Magma/Inferno scientific palette
	FColor EvaluateRoughnessColor(float NormVal)
	{
		NormVal = FMath::Clamp(NormVal, 0.0f, 1.0f);
		if (NormVal < 0.25f) // Smooth: Deep Navy Indigo -> Cyan
		{
			const float T = NormVal / 0.25f;
			return FColor(
				FMath::RoundToInt(FMath::Lerp(10.0f, 15.0f, T)),
				FMath::RoundToInt(FMath::Lerp(20.0f, 160.0f, T)),
				FMath::RoundToInt(FMath::Lerp(120.0f, 210.0f, T)),
				255);
		}
		else if (NormVal < 0.50f) // Low-Medium: Cyan -> Lime Green
		{
			const float T = (NormVal - 0.25f) / 0.25f;
			return FColor(
				FMath::RoundToInt(FMath::Lerp(15.0f, 90.0f, T)),
				FMath::RoundToInt(FMath::Lerp(160.0f, 225.0f, T)),
				FMath::RoundToInt(FMath::Lerp(210.0f, 35.0f, T)),
				255);
		}
		else if (NormVal < 0.75f) // Medium-High: Lime -> Bright Amber
		{
			const float T = (NormVal - 0.50f) / 0.25f;
			return FColor(
				FMath::RoundToInt(FMath::Lerp(90.0f, 255.0f, T)),
				FMath::RoundToInt(FMath::Lerp(225.0f, 140.0f, T)),
				FMath::RoundToInt(FMath::Lerp(35.0f, 10.0f, T)),
				255);
		}
		else // High-Extreme: Amber -> Hot Magenta
		{
			const float T = (NormVal - 0.75f) / 0.25f;
			return FColor(
				255,
				FMath::RoundToInt(FMath::Lerp(140.0f, 20.0f, T)),
				FMath::RoundToInt(FMath::Lerp(10.0f, 160.0f, T)),
				255);
		}
	}
}

bool FGRTGeoTIFFReader::DecodeRasterPixels(
	const FString& FilePath,
	int32 MaxDimension,
	TArray<FColor>& OutPixels,
	int32& OutWidth,
	int32& OutHeight,
	FString& OutError)
{
	OutError.Reset();
	OutPixels.Reset();
	OutWidth = 0;
	OutHeight = 0;

	EnsureGdalRegistered();

	GDALDatasetH Dataset = GDALOpen(TCHAR_TO_UTF8(*FilePath), GA_ReadOnly);
	if (!Dataset)
	{
		OutError = FString::Printf(TEXT("GDAL could not open '%s'."), *FilePath);
		return false;
	}
	ON_SCOPE_EXIT{ GDALClose(Dataset); };

	const int32 SrcWidth = GDALGetRasterXSize(Dataset);
	const int32 SrcHeight = GDALGetRasterYSize(Dataset);
	const int32 BandCount = GDALGetRasterCount(Dataset);

	if (SrcWidth <= 0 || SrcHeight <= 0 || BandCount <= 0)
	{
		OutError = TEXT("Invalid raster dimensions or band count.");
		return false;
	}

	// Calculate target dimensions maintaining aspect ratio
	OutWidth = SrcWidth;
	OutHeight = SrcHeight;
	if (MaxDimension > 0 && (OutWidth > MaxDimension || OutHeight > MaxDimension))
	{
		if (OutWidth >= OutHeight)
		{
			OutHeight = FMath::Max(1, FMath::RoundToInt((float)SrcHeight * ((float)MaxDimension / (float)SrcWidth)));
			OutWidth = MaxDimension;
		}
		else
		{
			OutWidth = FMath::Max(1, FMath::RoundToInt((float)SrcWidth * ((float)MaxDimension / (float)SrcHeight)));
			OutHeight = MaxDimension;
		}
	}

	const int32 TotalPixels = OutWidth * OutHeight;
	OutPixels.SetNumUninitialized(TotalPixels);

	GDALRasterBandH Band1 = GDALGetRasterBand(Dataset, 1);
	if (!Band1)
	{
		OutError = TEXT("Failed to acquire raster band 1.");
		return false;
	}

	const GDALDataType DataType = GDALGetRasterDataType(Band1);

	if (BandCount >= 3)
	{
		// 3, 4, or multi-spectral bands (RGB / RGBA / Satellite True-Color & False-Color)
		TArray<float> RedBand, GreenBand, BlueBand, AlphaBand;
		RedBand.SetNumUninitialized(TotalPixels);
		GreenBand.SetNumUninitialized(TotalPixels);
		BlueBand.SetNumUninitialized(TotalPixels);

		GDALRasterBandH Band2 = GDALGetRasterBand(Dataset, 2);
		GDALRasterBandH Band3 = GDALGetRasterBand(Dataset, 3);

		if (!Band2 || !Band3)
		{
			OutError = TEXT("Failed to acquire RGB bands from GeoTIFF.");
			return false;
		}

		if (GDALRasterIO(Band1, GF_Read, 0, 0, SrcWidth, SrcHeight, RedBand.GetData(), OutWidth, OutHeight, GDT_Float32, 0, 0) != CE_None ||
			GDALRasterIO(Band2, GF_Read, 0, 0, SrcWidth, SrcHeight, GreenBand.GetData(), OutWidth, OutHeight, GDT_Float32, 0, 0) != CE_None ||
			GDALRasterIO(Band3, GF_Read, 0, 0, SrcWidth, SrcHeight, BlueBand.GetData(), OutWidth, OutHeight, GDT_Float32, 0, 0) != CE_None)
		{
			OutError = TEXT("GDALRasterIO failed to read RGB bands.");
			return false;
		}

		bool bHasAlpha = false;
		if (BandCount >= 4)
		{
			GDALRasterBandH Band4 = GDALGetRasterBand(Dataset, 4);
			if (Band4)
			{
				AlphaBand.SetNumUninitialized(TotalPixels);
				if (GDALRasterIO(Band4, GF_Read, 0, 0, SrcWidth, SrcHeight, AlphaBand.GetData(), OutWidth, OutHeight, GDT_Float32, 0, 0) == CE_None)
				{
					bHasAlpha = true;
				}
			}
		}

		// Detect if this is 16-bit satellite reflectance (e.g. Sentinel-2 / Landsat L2A) or HDR data requiring auto-contrast
		float MaxR = 0.0f, MaxG = 0.0f, MaxB = 0.0f;
		for (int32 i = 0; i < TotalPixels; ++i)
		{
			if (FMath::IsFinite(RedBand[i])) MaxR = FMath::Max(MaxR, RedBand[i]);
			if (FMath::IsFinite(GreenBand[i])) MaxG = FMath::Max(MaxG, GreenBand[i]);
			if (FMath::IsFinite(BlueBand[i])) MaxB = FMath::Max(MaxB, BlueBand[i]);
		}

		const float GlobalMax = FMath::Max3(MaxR, MaxG, MaxB);
		const bool bNeedsSatelliteStretch = (GlobalMax > 255.0f);
		const float ScaleFactor = bNeedsSatelliteStretch ? (255.0f / FMath::Max(GlobalMax, 1.0f)) : 1.0f;

		for (int32 i = 0; i < TotalPixels; ++i)
		{
			float R = RedBand[i];
			float G = GreenBand[i];
			float B = BlueBand[i];

			if (!FMath::IsFinite(R)) R = 0.0f;
			if (!FMath::IsFinite(G)) G = 0.0f;
			if (!FMath::IsFinite(B)) B = 0.0f;

			if (bNeedsSatelliteStretch)
			{
				R *= ScaleFactor;
				G *= ScaleFactor;
				B *= ScaleFactor;
			}

			const uint8 OutR = (uint8)FMath::Clamp(FMath::RoundToInt(R), 0, 255);
			const uint8 OutG = (uint8)FMath::Clamp(FMath::RoundToInt(G), 0, 255);
			const uint8 OutB = (uint8)FMath::Clamp(FMath::RoundToInt(B), 0, 255);
			uint8 OutA = 255;
			if (bHasAlpha)
			{
				float A = AlphaBand[i];
				if (bNeedsSatelliteStretch) A *= ScaleFactor;
				OutA = (uint8)FMath::Clamp(FMath::RoundToInt(A), 0, 255);
			}

			OutPixels[i] = FColor(OutR, OutG, OutB, OutA);
		}
	}
	else if (DataType == GDT_Byte)
	{
		// 1 band Byte (Hillshade)
		TArray<uint8> GreyBand;
		GreyBand.SetNumUninitialized(TotalPixels);

		if (GDALRasterIO(Band1, GF_Read, 0, 0, SrcWidth, SrcHeight, GreyBand.GetData(), OutWidth, OutHeight, GDT_Byte, 0, 0) != CE_None)
		{
			OutError = TEXT("GDALRasterIO failed to read greyscale byte band.");
			return false;
		}

		for (int32 i = 0; i < TotalPixels; ++i)
		{
			const uint8 Val = GreyBand[i];
			OutPixels[i] = FColor(Val, Val, Val, 255);
		}
	}
	else
	{
		// 1 band Float32 (Slope, Aspect, Roughness, Elevation)
		TArray<float> FloatBand;
		FloatBand.SetNumUninitialized(TotalPixels);

		if (GDALRasterIO(Band1, GF_Read, 0, 0, SrcWidth, SrcHeight, FloatBand.GetData(), OutWidth, OutHeight, GDT_Float32, 0, 0) != CE_None)
		{
			OutError = TEXT("GDALRasterIO failed to read float raster band.");
			return false;
		}

		// Check layer characteristics: slope (0-90), aspect (0-360), roughness
		float MinVal = TNumericLimits<float>::Max();
		float MaxVal = -TNumericLimits<float>::Max();
		int32 HasNoData = 0;
		const double NoDataVal = GDALGetRasterNoDataValue(Band1, &HasNoData);

		for (int32 i = 0; i < TotalPixels; ++i)
		{
			const float V = FloatBand[i];
			if (!FMath::IsFinite(V) || (HasNoData && FMath::IsNearlyEqual(V, (float)NoDataVal, 1.0f)))
			{
				continue;
			}
			MinVal = FMath::Min(MinVal, V);
			MaxVal = FMath::Max(MaxVal, V);
		}

		const FString LowerName = FilePath.ToLower();
		const bool bIsAspect = LowerName.Contains(TEXT("aspect")) || (MaxVal > 90.0f && MaxVal <= 360.0f);
		const bool bIsSlope = LowerName.Contains(TEXT("slope")) || (MinVal >= 0.0f && MaxVal <= 90.0f && !bIsAspect);

		const float ValRange = FMath::Max(0.0001f, MaxVal - MinVal);

		for (int32 i = 0; i < TotalPixels; ++i)
		{
			const float V = FloatBand[i];
			if (!FMath::IsFinite(V) || (HasNoData && FMath::IsNearlyEqual(V, (float)NoDataVal, 1.0f)))
			{
				OutPixels[i] = FColor(0, 0, 0, 0);
				continue;
			}

			if (bIsAspect)
			{
				OutPixels[i] = EvaluateAspectColor(V);
			}
			else if (bIsSlope)
			{
				OutPixels[i] = EvaluateSlopeColor(V);
			}
			else
			{
				const float NormVal = FMath::Clamp((V - MinVal) / ValRange, 0.0f, 1.0f);
				OutPixels[i] = EvaluateRoughnessColor(NormVal);
			}
		}
	}

	return true;
}

UTexture2D* FGRTGeoTIFFReader::DecodeRasterToTexture(const FString& FilePath, int32 MaxDimension, FString& OutError)
{
	TArray<FColor> Pixels;
	int32 OutWidth = 0, OutHeight = 0;

	if (!DecodeRasterPixels(FilePath, MaxDimension, Pixels, OutWidth, OutHeight, OutError))
	{
		return nullptr;
	}

	const int32 TotalPixels = OutWidth * OutHeight;
	UTexture2D* OutTexture = UTexture2D::CreateTransient(OutWidth, OutHeight, PF_B8G8R8A8);
	if (!OutTexture)
	{
		OutError = TEXT("Failed to create transient UTexture2D.");
		return nullptr;
	}

	OutTexture->SRGB = true;
	OutTexture->CompressionSettings = TextureCompressionSettings::TC_Default;
	OutTexture->Filter = TextureFilter::TF_Bilinear;

	void* TextureData = OutTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), TotalPixels * sizeof(FColor));
	OutTexture->GetPlatformData()->Mips[0].BulkData.Unlock();

	OutTexture->UpdateResource();

	UE_LOG(LogGRT, Log, TEXT("GRT: Decoded GeoTIFF texture '%s' -> %dx%d"),
		*FilePath, OutWidth, OutHeight);

	return OutTexture;
}
