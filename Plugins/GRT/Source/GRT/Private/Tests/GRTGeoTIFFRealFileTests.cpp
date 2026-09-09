// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "IO/GRTGeoTIFFReader.h"

/**
 * Exercises FGRTGeoTIFFReader::LoadFromFile against real, machine-local GDAL-derived GeoTIFF
 * files rather than synthetic fixtures. The reader has only ever been build-verified against
 * hand-built minimal TIFFs, so this is the first check against real-world tag combinations (tiled
 * layout, LZW compression, both integer and IEEE-float sample formats).
 *
 * The test data lives outside the repo (C:/Projects/viz/) and is not expected to exist on CI or
 * other developers' machines, so a missing file is a graceful skip (return true), not a failure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTGeoTIFFRealFileTest, "GRT.IO.GeoTIFFRealFile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GRTGeoTIFFRealFileTest_Private
{
	constexpr int32 ExpectedWidth = 16797;
	constexpr int32 ExpectedHeight = 7493;
	constexpr double ExpectedOriginLon = 76.15569443333331;
	constexpr double ExpectedOriginLat = 32.06041667777777;
	constexpr double OriginTolerance = 1e-9;
}

bool FGRTGeoTIFFRealFileTest::RunTest(const FString& Parameters)
{
	using namespace GRTGeoTIFFRealFileTest_Private;

	// One (Path, bool bIsSlow) entry per candidate file. The hillshade file is the fast/primary
	// case; the slope file is much larger (32-bit float samples, ~4x the bytes) and is attempted
	// as a secondary, best-effort check.
	struct FCandidate
	{
		FString Path;
		const TCHAR* Label;
	};

	const TArray<FCandidate> Candidates = {
		{ TEXT("C:/Projects/viz/viz.hh_hillshade.tif"), TEXT("hillshade (primary)") },
		{ TEXT("C:/Projects/viz/viz.hh_slope.tif"), TEXT("slope (secondary)") },
	};

	bool bRanAtLeastOne = false;

	for (const FCandidate& Candidate : Candidates)
	{
		if (!FPaths::FileExists(Candidate.Path))
		{
			UE_LOG(LogTemp, Display, TEXT("[GeoTIFFRealFile] Skipping %s -- file not present at %s (expected on machines without the local test-data checkout)."),
				Candidate.Label, *Candidate.Path);
			continue;
		}

		bRanAtLeastOne = true;

		UE_LOG(LogTemp, Display, TEXT("[GeoTIFFRealFile] Loading %s: %s"), Candidate.Label, *Candidate.Path);

		FGRTGeoTIFFRaster Raster;
		FString LoadError;
		const double StartSeconds = FPlatformTime::Seconds();
		const bool bLoaded = FGRTGeoTIFFReader::LoadFromFile(Candidate.Path, Raster, LoadError);
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;

		UE_LOG(LogTemp, Display, TEXT("[GeoTIFFRealFile] %s: LoadFromFile returned %s in %.2f s"),
			Candidate.Label, bLoaded ? TEXT("true") : TEXT("false"), ElapsedSeconds);

		if (!bLoaded)
		{
			AddError(FString::Printf(TEXT("[GeoTIFFRealFile] %s: LoadFromFile failed: %s"), Candidate.Label, *LoadError));
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("[GeoTIFFRealFile] %s: %dx%d, elevation [%f, %f], PixelToCrs(0,0)=(%.17g, %.17g)"),
			Candidate.Label, Raster.Width, Raster.Height, Raster.ElevationMinMeters, Raster.ElevationMaxMeters,
			Raster.PixelToCrs(0.0, 0.0).X, Raster.PixelToCrs(0.0, 0.0).Y);

		TestEqual(FString::Printf(TEXT("%s: Width"), Candidate.Label), Raster.Width, ExpectedWidth);
		TestEqual(FString::Printf(TEXT("%s: Height"), Candidate.Label), Raster.Height, ExpectedHeight);
		TestTrue(FString::Printf(TEXT("%s: bIsGeographicWGS84"), Candidate.Label), Raster.bIsGeographicWGS84);

		TestTrue(FString::Printf(TEXT("%s: ElevationMinMeters is finite"), Candidate.Label), FMath::IsFinite(Raster.ElevationMinMeters));
		TestTrue(FString::Printf(TEXT("%s: ElevationMaxMeters is finite"), Candidate.Label), FMath::IsFinite(Raster.ElevationMaxMeters));
		TestTrue(FString::Printf(TEXT("%s: ElevationMinMeters <= ElevationMaxMeters"), Candidate.Label), Raster.ElevationMinMeters <= Raster.ElevationMaxMeters);

		const FVector2D Origin = Raster.PixelToCrs(0.0, 0.0);
		TestTrue(FString::Printf(TEXT("%s: PixelToCrs(0,0).X matches expected origin longitude"), Candidate.Label),
			FMath::IsNearlyEqual(Origin.X, ExpectedOriginLon, OriginTolerance));
		TestTrue(FString::Printf(TEXT("%s: PixelToCrs(0,0).Y matches expected origin latitude"), Candidate.Label),
			FMath::IsNearlyEqual(Origin.Y, ExpectedOriginLat, OriginTolerance));
	}

	if (!bRanAtLeastOne)
	{
		UE_LOG(LogTemp, Display, TEXT("[GeoTIFFRealFile] No local test-data files found under C:/Projects/viz/ -- skipping (this is expected outside the original dev machine)."));
	}

	return true;
}
