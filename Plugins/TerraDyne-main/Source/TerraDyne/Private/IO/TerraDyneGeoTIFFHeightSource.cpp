// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneGeoTIFFHeightSource.h"

#include "TerraDyneModule.h"
#include "Engine/World.h"
#include "CesiumGeoreference.h"

namespace
{
	// WGS84 ellipsoid constants (the only datum this reader accepts, per TerraDyneGeoTIFFReader).
	constexpr double UTM_A = 6378137.0;                  // semi-major axis, metres
	constexpr double UTM_F = 1.0 / 298.257223563;        // flattening
	constexpr double UTM_E2 = 2.0 * UTM_F - UTM_F * UTM_F; // first eccentricity squared
	constexpr double UTM_K0 = 0.9996;                    // UTM scale factor

	double CentralMeridianRad(int32 Zone)
	{
		return FMath::DegreesToRadians((double)(Zone * 6 - 183));
	}

	/**
	 * Forward Transverse Mercator (Snyder's standard UTM series, truncated at the terms that matter
	 * for sub-metre accuracy across one UTM zone -- ample for placing terrain, not survey-grade).
	 */
	void LonLatToUTM(double LonDeg, double LatDeg, int32 Zone, bool bNorth, double& OutEasting, double& OutNorthing)
	{
		const double Phi = FMath::DegreesToRadians(LatDeg);
		const double Lambda = FMath::DegreesToRadians(LonDeg);
		const double Lambda0 = CentralMeridianRad(Zone);

		const double EPrime2 = UTM_E2 / (1.0 - UTM_E2);
		const double SinPhi = FMath::Sin(Phi);
		const double CosPhi = FMath::Cos(Phi);
		const double TanPhi = FMath::Tan(Phi);

		const double N = UTM_A / FMath::Sqrt(1.0 - UTM_E2 * SinPhi * SinPhi);
		const double T = TanPhi * TanPhi;
		const double C = EPrime2 * CosPhi * CosPhi;
		const double A = (Lambda - Lambda0) * CosPhi;

		const double M = UTM_A * (
			(1.0 - UTM_E2 / 4.0 - 3.0 * UTM_E2 * UTM_E2 / 64.0 - 5.0 * UTM_E2 * UTM_E2 * UTM_E2 / 256.0) * Phi
			- (3.0 * UTM_E2 / 8.0 + 3.0 * UTM_E2 * UTM_E2 / 32.0 + 45.0 * UTM_E2 * UTM_E2 * UTM_E2 / 1024.0) * FMath::Sin(2.0 * Phi)
			+ (15.0 * UTM_E2 * UTM_E2 / 256.0 + 45.0 * UTM_E2 * UTM_E2 * UTM_E2 / 1024.0) * FMath::Sin(4.0 * Phi)
			- (35.0 * UTM_E2 * UTM_E2 * UTM_E2 / 3072.0) * FMath::Sin(6.0 * Phi));

		OutEasting = UTM_K0 * N * (A + (1.0 - T + C) * FMath::Pow(A, 3.0) / 6.0
			+ (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * EPrime2) * FMath::Pow(A, 5.0) / 120.0) + 500000.0;

		OutNorthing = UTM_K0 * (M + N * TanPhi * (A * A / 2.0
			+ (5.0 - T + 9.0 * C + 4.0 * C * C) * FMath::Pow(A, 4.0) / 24.0
			+ (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * EPrime2) * FMath::Pow(A, 6.0) / 720.0));

		if (!bNorth)
		{
			OutNorthing += 10000000.0;
		}
	}

	/** Inverse Transverse Mercator -- the standard UTM-to-lon/lat series (Snyder). */
	void UTMToLonLat(double Easting, double Northing, int32 Zone, bool bNorth, double& OutLonDeg, double& OutLatDeg)
	{
		const double Lambda0 = CentralMeridianRad(Zone);
		const double EPrime2 = UTM_E2 / (1.0 - UTM_E2);
		const double E1 = (1.0 - FMath::Sqrt(1.0 - UTM_E2)) / (1.0 + FMath::Sqrt(1.0 - UTM_E2));

		const double X = Easting - 500000.0;
		const double Y = bNorth ? Northing : (Northing - 10000000.0);

		const double M = Y / UTM_K0;
		const double Mu = M / (UTM_A * (1.0 - UTM_E2 / 4.0 - 3.0 * UTM_E2 * UTM_E2 / 64.0 - 5.0 * UTM_E2 * UTM_E2 * UTM_E2 / 256.0));

		const double Phi1 = Mu
			+ (3.0 * E1 / 2.0 - 27.0 * FMath::Pow(E1, 3.0) / 32.0) * FMath::Sin(2.0 * Mu)
			+ (21.0 * E1 * E1 / 16.0 - 55.0 * FMath::Pow(E1, 4.0) / 32.0) * FMath::Sin(4.0 * Mu)
			+ (151.0 * FMath::Pow(E1, 3.0) / 96.0) * FMath::Sin(6.0 * Mu)
			+ (1097.0 * FMath::Pow(E1, 4.0) / 512.0) * FMath::Sin(8.0 * Mu);

		const double SinPhi1 = FMath::Sin(Phi1);
		const double CosPhi1 = FMath::Cos(Phi1);
		const double TanPhi1 = FMath::Tan(Phi1);

		const double C1 = EPrime2 * CosPhi1 * CosPhi1;
		const double T1 = TanPhi1 * TanPhi1;
		const double N1 = UTM_A / FMath::Sqrt(1.0 - UTM_E2 * SinPhi1 * SinPhi1);
		const double R1 = UTM_A * (1.0 - UTM_E2) / FMath::Pow(1.0 - UTM_E2 * SinPhi1 * SinPhi1, 1.5);
		const double D = X / (N1 * UTM_K0);

		const double Phi = Phi1 - (N1 * TanPhi1 / R1) * (D * D / 2.0
			- (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1 * C1 - 9.0 * EPrime2) * FMath::Pow(D, 4.0) / 24.0
			+ (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1 * T1 - 252.0 * EPrime2 - 3.0 * C1 * C1) * FMath::Pow(D, 6.0) / 720.0);

		const double Lambda = Lambda0 + (D
			- (1.0 + 2.0 * T1 + C1) * FMath::Pow(D, 3.0) / 6.0
			+ (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1 * C1 + 8.0 * EPrime2 + 24.0 * T1 * T1) * FMath::Pow(D, 5.0) / 120.0) / CosPhi1;

		OutLatDeg = FMath::RadiansToDegrees(Phi);
		OutLonDeg = FMath::RadiansToDegrees(Lambda);
	}
}

bool FTerraDyneGeoTIFFHeightSource::WorldXYToRasterCrs(double WorldX, double WorldY, double& OutCrsX, double& OutCrsY) const
{
	ACesiumGeoreference* Ref = Georeference.Get();
	if (!Ref) return false;

	const FVector LonLatHeight = Ref->TransformUnrealPositionToLongitudeLatitudeHeight(FVector(WorldX, WorldY, 0.0));

	if (Raster.bIsGeographicWGS84)
	{
		OutCrsX = LonLatHeight.X; // longitude
		OutCrsY = LonLatHeight.Y; // latitude
		return true;
	}
	if (Raster.bIsUTM)
	{
		LonLatToUTM(LonLatHeight.X, LonLatHeight.Y, Raster.UTMZone, Raster.bUTMNorth, OutCrsX, OutCrsY);
		return true;
	}
	return false;
}

bool FTerraDyneGeoTIFFHeightSource::Initialize(
	FTerraDyneGeoTIFFRaster&& InRaster,
	UWorld* World,
	bool bEstablishGeoreferenceOrigin,
	float HeadroomFraction,
	float OverrideZScaleOrZero,
	FString& OutError)
{
	Raster = MoveTemp(InRaster);

	if (!World)
	{
		OutError = TEXT("No world to resolve a CesiumGeoreference in.");
		return false;
	}

	// Finds an existing one first (tag DEFAULT_GEOREFERENCE, then name CesiumGeoreferenceDefault,
	// then any instance), creating one only if the level truly has none.
	ACesiumGeoreference* Ref = ACesiumGeoreference::GetDefaultGeoreference(World);
	if (!Ref)
	{
		OutError = TEXT("Could not resolve or create a CesiumGeoreference.");
		return false;
	}
	Georeference = Ref;

	// The raster's own geographic centre and corners, via its own CRS -- lon/lat directly if
	// geographic, or through the inverse-UTM series if projected.
	auto RasterCrsToLonLat = [this](double CrsX, double CrsY, double& OutLon, double& OutLat)
	{
		if (Raster.bIsGeographicWGS84)
		{
			OutLon = CrsX;
			OutLat = CrsY;
		}
		else
		{
			UTMToLonLat(CrsX, CrsY, Raster.UTMZone, Raster.bUTMNorth, OutLon, OutLat);
		}
	};

	const FVector2D CentreCrs = Raster.PixelToCrs((Raster.Width - 1) * 0.5, (Raster.Height - 1) * 0.5);
	double CentreLon = 0.0, CentreLat = 0.0;
	RasterCrsToLonLat(CentreCrs.X, CentreCrs.Y, CentreLon, CentreLat);

	// Elevation band first -- the georeference origin below needs the centre altitude, and both need
	// the valid-sample range the reader computed with NoData already excluded.
	//
	// Reserve HeadroomFraction of ZScale below and above the raster's real range, mirroring
	// TerraDyneLayers::ProceduralFloor/ProceduralRelief's own reserved-headroom idea, so sculpting
	// still has [0,1] room to raise/lower imported terrain rather than clipping instantly.
	const double RangeMeters = FMath::Max(1.0, Raster.ElevationMaxMeters - Raster.ElevationMinMeters);
	const float ClampedHeadroom = FMath::Clamp(HeadroomFraction, 0.0f, 0.4f);
	ZScale = OverrideZScaleOrZero > 0.0f
		? OverrideZScaleOrZero
		: (float)(RangeMeters * 100.0 / (1.0 - 2.0 * ClampedHeadroom)); // metres -> cm, then widen for headroom
	ElevationFloorMeters = Raster.ElevationMinMeters - RangeMeters * ClampedHeadroom;
	ElevationRangeMeters = (ZScale / 100.0); // cm ZScale back to metres, matching the widened band

	// Altitude that Unreal Z = 0 will mean. Taken from the DEM's own centre sample rather than the
	// midpoint of its range, so the origin sits on real ground the player can stand on: on a DEM
	// spanning a valley floor and a ridge line, the range midpoint is thin air halfway up the slope.
	// Falls back to the range midpoint only when the centre pixel is itself NoData.
	OriginElevationMeters = SampleElevationMetersAtPixelOrFallback(
		(Raster.Width - 1) * 0.5, (Raster.Height - 1) * 0.5,
		(Raster.ElevationMinMeters + Raster.ElevationMaxMeters) * 0.5);

	if (bEstablishGeoreferenceOrigin)
	{
		// Height is the DEM's real centre altitude, not 0. This is what makes the import land at its
		// true elevation instead of being flattened onto the sea-level tangent plane -- Cesium then
		// maps Unreal Z = 0 to that altitude, and GetChunkWorldZOffset() positions the band around it.
		Ref->SetOriginLongitudeLatitudeHeight(FVector(CentreLon, CentreLat, OriginElevationMeters));
	}

	// Footprint: transform all four corner pixels (not just two, since a rotated/sheared affine or
	// a georeference with non-trivial orientation could otherwise produce a misleading AABB from
	// only opposite corners) into Unreal world space and take their bounds.
	WorldFootprintXY = FBox2D(ForceInit);
	const FVector2D CornerPixels[4] = {
		FVector2D(0.0, 0.0),
		FVector2D(Raster.Width - 1.0, 0.0),
		FVector2D(0.0, Raster.Height - 1.0),
		FVector2D(Raster.Width - 1.0, Raster.Height - 1.0)
	};
	for (const FVector2D& PixelCorner : CornerPixels)
	{
		const FVector2D Crs = Raster.PixelToCrs(PixelCorner.X, PixelCorner.Y);
		double Lon = 0.0, Lat = 0.0;
		RasterCrsToLonLat(Crs.X, Crs.Y, Lon, Lat);
		const FVector WorldPos = Ref->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Lon, Lat, 0.0));
		WorldFootprintXY += FVector2D(WorldPos.X, WorldPos.Y);
	}

	UE_LOG(LogTerraDyne, Log,
		TEXT("GeoTIFF height source initialized: footprint [%.0f,%.0f] to [%.0f,%.0f] cm, ZScale=%.0f, ")
		TEXT("elevation [%.1f,%.1f]m, origin altitude %.1fm (Unreal Z=0), chunk Z offset %.0f cm"),
		WorldFootprintXY.Min.X, WorldFootprintXY.Min.Y, WorldFootprintXY.Max.X, WorldFootprintXY.Max.Y, ZScale,
		Raster.ElevationMinMeters, Raster.ElevationMaxMeters, OriginElevationMeters, GetChunkWorldZOffset());

	return true;
}

double FTerraDyneGeoTIFFHeightSource::SampleElevationMetersAtPixelOrFallback(
	double Col, double Row, double Fallback) const
{
	if (Raster.Width <= 0 || Raster.Height <= 0 || Raster.ElevationMeters.Num() == 0)
	{
		return Fallback;
	}

	const int32 C = FMath::Clamp((int32)FMath::RoundToDouble(Col), 0, Raster.Width - 1);
	const int32 R = FMath::Clamp((int32)FMath::RoundToDouble(Row), 0, Raster.Height - 1);
	const int32 Index = R * Raster.Width + C;
	if (!Raster.ElevationMeters.IsValidIndex(Index))
	{
		return Fallback;
	}

	const float Sample = Raster.ElevationMeters[Index];
	return Raster.IsValidSample(Sample) ? (double)Sample : Fallback;
}

bool FTerraDyneGeoTIFFHeightSource::TrySampleWorldXY(double WorldX, double WorldY, float& OutNormalized01) const
{
	if (!WorldFootprintXY.IsInside(FVector2D(WorldX, WorldY)))
	{
		return false;
	}

	double CrsX = 0.0, CrsY = 0.0;
	if (!WorldXYToRasterCrs(WorldX, WorldY, CrsX, CrsY))
	{
		return false;
	}

	const FVector2D Pixel = Raster.CrsToPixel(CrsX, CrsY);
	if (Pixel.X < 0.0 || Pixel.Y < 0.0 || Pixel.X > Raster.Width - 1 || Pixel.Y > Raster.Height - 1)
	{
		return false;
	}

	// Bilinear sample.
	const int32 Col0 = FMath::Clamp((int32)FMath::FloorToDouble(Pixel.X), 0, Raster.Width - 1);
	const int32 Row0 = FMath::Clamp((int32)FMath::FloorToDouble(Pixel.Y), 0, Raster.Height - 1);
	const int32 Col1 = FMath::Min(Col0 + 1, Raster.Width - 1);
	const int32 Row1 = FMath::Min(Row0 + 1, Raster.Height - 1);
	const double FracX = Pixel.X - Col0;
	const double FracY = Pixel.Y - Row0;

	auto SampleAt = [this](int32 Col, int32 Row) -> float
	{
		return Raster.ElevationMeters[(int64)Row * Raster.Width + Col];
	};

	const float S00 = SampleAt(Col0, Row0);
	const float S10 = SampleAt(Col1, Row0);
	const float S01 = SampleAt(Col0, Row1);
	const float S11 = SampleAt(Col1, Row1);

	// Refuse the sample outright if ANY contributing cell is NoData, rather than interpolating it.
	// Blending a -9999 sentinel with real neighbours does not produce a plausible in-between height,
	// it produces a hole punched kilometres deep, and it spreads one bad cell across every texel that
	// touches it. Reporting failure lets the caller decide (fill from valid neighbours, or leave the
	// chunk procedural) instead of baking a spike into the mesh.
	if (!Raster.IsValidSample(S00) || !Raster.IsValidSample(S10)
		|| !Raster.IsValidSample(S01) || !Raster.IsValidSample(S11))
	{
		return false;
	}

	const double Top = FMath::Lerp((double)S00, (double)S10, FracX);
	const double Bottom = FMath::Lerp((double)S01, (double)S11, FracX);
	const double ElevationMeters = FMath::Lerp(Top, Bottom, FracY);

	OutNormalized01 = (float)FMath::Clamp((ElevationMeters - ElevationFloorMeters) / ElevationRangeMeters, 0.0, 1.0);
	return true;
}

bool FTerraDyneGeoTIFFHeightSource::TrySampleRasterPixel(double WorldX, double WorldY, double& OutPixelX, double& OutPixelY) const
{
	if (!WorldFootprintXY.IsInside(FVector2D(WorldX, WorldY)))
	{
		return false;
	}

	double CrsX = 0.0, CrsY = 0.0;
	if (!WorldXYToRasterCrs(WorldX, WorldY, CrsX, CrsY))
	{
		return false;
	}

	const FVector2D Pixel = Raster.CrsToPixel(CrsX, CrsY);
	if (Pixel.X < 0.0 || Pixel.Y < 0.0 || Pixel.X > Raster.Width - 1 || Pixel.Y > Raster.Height - 1)
	{
		return false;
	}

	OutPixelX = Pixel.X;
	OutPixelY = Pixel.Y;
	return true;
}
