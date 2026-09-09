// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "IO/GRTGeoTIFFReader.h"
#include "IO/GRTRasterLayers.h"
#include "IO/GRTArchiveHelper.h"
#include "Core/GRTManager.h"
#include "World/GRTChunk.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// Validates the multi-layer compatibility rules against a REAL matched raster set produced by
// gdaldem from one source DEM. The interesting case is not that matching files match -- it is that
// the DEM and its own derived products do NOT share an identical geotransform: the DEM is written
// RasterPixelIsPoint and the derived products RasterPixelIsArea, which offsets the reported origin
// by exactly half a pixel. A naive exact-match rule rejects a user's own matched set, so the
// half-pixel allowance is the whole point of the check and is asserted here directly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTRasterLayerCompatibilityTest,
	"GRT.Raster.LayerCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GRTRasterLayerTest_Private
{
	const TCHAR* SetDir = TEXT("C:/Projects/Test/viz (1)");

	struct FCandidate
	{
		const TCHAR* FileName;
		EGRTRasterLayer Layer;
		int32 ExpectedBands;
	};

	const TArray<FCandidate>& Candidates()
	{
		static const TArray<FCandidate> Files = {
			{ TEXT("output_hh.tif"),                EGRTRasterLayer::Elevation,      1 },
			{ TEXT("viz.hh_hillshade.tif"),         EGRTRasterLayer::Hillshade,      1 },
			{ TEXT("viz.hh_color-relief.tif"),      EGRTRasterLayer::ColorRelief,    3 },
			{ TEXT("viz.hh_hillshade-color.tif"),   EGRTRasterLayer::ColorHillshade, 3 },
			{ TEXT("viz.hh_slope.tif"),             EGRTRasterLayer::Slope,          1 },
			{ TEXT("viz.hh_aspect.tif"),            EGRTRasterLayer::Aspect,         1 },
			{ TEXT("viz.hh_roughness.tif"),         EGRTRasterLayer::Roughness,      1 },
		};
		return Files;
	}
}

bool FGRTRasterLayerCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace GRTRasterLayerTest_Private;

	TMap<EGRTRasterLayer, FGRTRasterInfo> Probed;

	for (const FCandidate& Candidate : Candidates())
	{
		const FString Path = FPaths::Combine(SetDir, Candidate.FileName);
		if (!FPaths::FileExists(Path))
		{
			AddInfo(FString::Printf(TEXT("Skipping %s -- not present on this machine."), Candidate.FileName));
			continue;
		}

		FGRTRasterInfo Info;
		FString Error;
		const double Start = FPlatformTime::Seconds();
		const bool bProbed = FGRTGeoTIFFReader::ProbeMetadata(Path, Info, Error);
		const double Elapsed = FPlatformTime::Seconds() - Start;

		if (!bProbed)
		{
			AddError(FString::Printf(TEXT("ProbeMetadata failed for %s: %s"), Candidate.FileName, *Error));
			continue;
		}

		AddInfo(FString::Printf(TEXT("%s: %dx%d, %d band(s), origin=(%.10f,%.10f), pixelIsPoint=%d, probed in %.1f ms"),
			Candidate.FileName, Info.Width, Info.Height, Info.BandCount, Info.A0, Info.B0,
			Info.bPixelIsPoint ? 1 : 0, Elapsed * 1000.0));

		// The probe must be genuinely cheap -- that is its entire reason to exist, given these files
		// are hundreds of megabytes and a full decode takes seconds.
		TestTrue(FString::Printf(TEXT("%s probe should be fast (header only)"), Candidate.FileName), Elapsed < 2.0);
		TestEqual(FString::Printf(TEXT("%s band count"), Candidate.FileName), Info.BandCount, Candidate.ExpectedBands);

		FString BandReason;
		TestTrue(
			FString::Printf(TEXT("%s band count should be valid for its layer (%s)"), Candidate.FileName, *BandReason),
			GRTRasterLayers::IsBandCountValidFor(Candidate.Layer, Info.BandCount, BandReason));

		Probed.Add(Candidate.Layer, Info);
	}

	const FGRTRasterInfo* Elevation = Probed.Find(EGRTRasterLayer::Elevation);
	if (!Elevation)
	{
		AddInfo(TEXT("Elevation raster not available; skipping the cross-layer compatibility assertions."));
		return true;
	}

	// Every derived product must be accepted against the DEM despite the half-pixel origin offset.
	for (const TPair<EGRTRasterLayer, FGRTRasterInfo>& Pair : Probed)
	{
		if (Pair.Key == EGRTRasterLayer::Elevation) continue;

		const double OffsetPixels = FMath::Abs(Elevation->A0 - Pair.Value.A0) / Elevation->PixelSizeCrs();

		FString Reason;
		const bool bCompatible = Elevation->IsCompatibleWith(Pair.Value, Reason);

		AddInfo(FString::Printf(TEXT("%s vs Elevation: origin offset %.3f px -> %s%s"),
			*GRTRasterLayers::GetDisplayName(Pair.Key).ToString(), OffsetPixels,
			bCompatible ? TEXT("COMPATIBLE") : TEXT("REJECTED: "), bCompatible ? TEXT("") : *Reason));

		TestTrue(
			FString::Printf(TEXT("%s should be compatible with its own source DEM"),
				*GRTRasterLayers::GetDisplayName(Pair.Key).ToString()),
			bCompatible);
	}

	// A genuinely different area must be refused. Shifting the origin by ten pixels is well past the
	// half-pixel convention allowance while leaving size, scale and CRS identical -- i.e. exactly the
	// "same product, wrong place" mistake the check exists to catch.
	{
		FGRTRasterInfo Elsewhere = *Elevation;
		Elsewhere.A0 += Elevation->PixelSizeCrs() * 10.0;

		FString Reason;
		const bool bCompatible = Elevation->IsCompatibleWith(Elsewhere, Reason);
		AddInfo(FString::Printf(TEXT("Ten-pixel-shifted raster -> %s%s"),
			bCompatible ? TEXT("WRONGLY ACCEPTED") : TEXT("correctly rejected: "), *Reason));
		TestFalse(TEXT("A raster from a different area must be rejected"), bCompatible);
	}

	// Different pixel dimensions must be refused outright.
	{
		FGRTRasterInfo Resized = *Elevation;
		Resized.Width += 1;

		FString Reason;
		TestFalse(TEXT("A raster with different dimensions must be rejected"),
			Elevation->IsCompatibleWith(Resized, Reason));
	}

	// A three-band colour image must never be accepted as elevation, however well it aligns.
	{
		FString Reason;
		TestFalse(TEXT("A 3-band RGB raster must be rejected as an Elevation layer"),
			GRTRasterLayers::IsBandCountValidFor(EGRTRasterLayer::Elevation, 3, Reason));
		AddInfo(FString::Printf(TEXT("3-band as Elevation -> rejected: %s"), *Reason));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTRasterWorkflowTest,
	"GRT.Raster.Workflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTRasterWorkflowTest::RunTest(const FString& Parameters)
{
	using namespace GRTRasterLayerTest_Private;

	// 1. Archive detection and resolution
	{
		TestTrue(TEXT("IsArchiveFile should recognize .tar.gz"),
			FGRTArchiveHelper::IsArchiveFile(TEXT("C:/Projects/Test/rasters_COP30.tar.gz")));
		TestTrue(TEXT("HasSupportedExtension should accept .tif"),
			FGRTArchiveHelper::HasSupportedExtension(TEXT("output_hh.tif")));
		TestTrue(TEXT("HasSupportedExtension should accept .tar.gz"),
			FGRTArchiveHelper::HasSupportedExtension(TEXT("rasters_COP30.tar.gz")));

		const FString ArchivePath = TEXT("C:/Projects/Test/rasters_COP30.tar.gz");
		if (FPaths::FileExists(ArchivePath))
		{
			FString ResolvedPath;
			FString ExtractErr;
			const bool bResolved = FGRTArchiveHelper::ResolveOrExtractRaster(
				ArchivePath, EGRTRasterLayer::Elevation, ResolvedPath, ExtractErr);

			TestTrue(TEXT("ResolveOrExtractRaster should extract COP30 archive"), bResolved);
			TestTrue(TEXT("Extracted raster should exist on disk"), FPaths::FileExists(ResolvedPath));
			AddInfo(FString::Printf(TEXT("COP30 archive resolved to: %s"), *ResolvedPath));
		}
	}

	// 2. Manager import rules, gating, compatibility, and unloading
	{
		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		TestNotNull(TEXT("World must be valid"), World);
		if (!World) return false;

		AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
		TestNotNull(TEXT("Manager instance should be valid"), Mgr);
		if (!Mgr) return false;

		Mgr->bShowWorldModeChoiceOnBeginPlay = false;

		// Rule 1: Cannot import overlay before Elevation
		const FString HillshadePath = FPaths::Combine(SetDir, TEXT("viz.hh_hillshade.tif"));
		if (FPaths::FileExists(HillshadePath))
		{
			FString OutError;
			const bool bGatingCaught = Mgr->ImportRasterLayer(EGRTRasterLayer::Hillshade, HillshadePath, OutError);
			TestFalse(TEXT("Importing Hillshade before Elevation DEM must fail"), bGatingCaught);
			TestTrue(TEXT("Error message should mention importing Elevation first"),
				OutError.Contains(TEXT("Elevation (DEM) layer first")));
			AddInfo(FString::Printf(TEXT("Gating rule verified: %s"), *OutError));
		}

		// Import Elevation DEM
		const FString DemPath = FPaths::Combine(SetDir, TEXT("output_hh.tif"));
		if (FPaths::FileExists(DemPath))
		{
			FString DemError;
			const bool bDemImported = Mgr->ImportRasterLayer(EGRTRasterLayer::Elevation, DemPath, DemError);
			TestTrue(TEXT("Importing Elevation DEM should succeed"), bDemImported);
			TestTrue(TEXT("HasElevationLayer should be true"), Mgr->HasElevationLayer());

			// Import all remaining 6 overlay layers one by one
			for (const FCandidate& Candidate : Candidates())
			{
				if (Candidate.Layer == EGRTRasterLayer::Elevation) continue;

				const FString LayerPath = FPaths::Combine(SetDir, Candidate.FileName);
				if (!FPaths::FileExists(LayerPath)) continue;

				FString LayerError;
				const bool bLayerImported = Mgr->ImportRasterLayer(Candidate.Layer, LayerPath, LayerError);
				TestTrue(FString::Printf(TEXT("Importing %s should succeed and match DEM"), Candidate.FileName), bLayerImported);
				TestTrue(FString::Printf(TEXT("Layer %s should be marked imported"), Candidate.FileName),
					Mgr->IsRasterLayerImported(Candidate.Layer));
			}

			// Verify all slots are populated
			for (int32 i = 0; i < (int32)EGRTRasterLayer::MAX; ++i)
			{
				const EGRTRasterLayer Layer = (EGRTRasterLayer)i;
				TestTrue(FString::Printf(TEXT("Slot %s should be imported"), *GRTRasterLayers::GetDisplayName(Layer).ToString()),
					Mgr->IsRasterLayerImported(Layer));
			}

			// Unload single layer (Slope)
			Mgr->UnloadRasterLayer(EGRTRasterLayer::Slope);
			TestFalse(TEXT("Slope should be unloaded"), Mgr->IsRasterLayerImported(EGRTRasterLayer::Slope));
			TestTrue(TEXT("Elevation DEM should remain imported"), Mgr->HasElevationLayer());
			TestTrue(TEXT("Hillshade should remain imported"), Mgr->IsRasterLayerImported(EGRTRasterLayer::Hillshade));

			// Re-import Slope
			const FString SlopePath = FPaths::Combine(SetDir, TEXT("viz.hh_slope.tif"));
			FString SlopeErr;
			Mgr->ImportRasterLayer(EGRTRasterLayer::Slope, SlopePath, SlopeErr);
			TestTrue(TEXT("Slope re-import should succeed"), Mgr->IsRasterLayerImported(EGRTRasterLayer::Slope));

			// Unload Elevation -> must cascade and unload everything
			Mgr->UnloadRasterLayer(EGRTRasterLayer::Elevation);
			for (int32 i = 0; i < (int32)EGRTRasterLayer::MAX; ++i)
			{
				const EGRTRasterLayer Layer = (EGRTRasterLayer)i;
				TestFalse(FString::Printf(TEXT("Slot %s should be unloaded after DEM unload"), *GRTRasterLayers::GetDisplayName(Layer).ToString()),
					Mgr->IsRasterLayerImported(Layer));
			}

			// 3. Re-import Elevation and perform full scene load + chunk geometry validation
			FString ReImportErr;
			const bool bReImported = Mgr->ImportRasterLayer(EGRTRasterLayer::Elevation, DemPath, ReImportErr);
			TestTrue(TEXT("Re-importing Elevation DEM should succeed"), bReImported);

			FString LoadErr;
			const double LoadStart = FPlatformTime::Seconds();
			const bool bLoaded = Mgr->LoadRasterLayersIntoScene(LoadErr);
			const double LoadElapsed = FPlatformTime::Seconds() - LoadStart;

			TestTrue(FString::Printf(TEXT("LoadRasterLayersIntoScene should succeed (err: %s)"), *LoadErr), bLoaded);
			AddInfo(FString::Printf(TEXT("DEM decode & georeference took %.2f s. Marked chunks: %d"),
				LoadElapsed, Mgr->GetImportedChunkCoords().Num()));

			TestTrue(TEXT("Manager should have marked imported chunk coordinates"), Mgr->GetImportedChunkCoords().Num() > 0);
			TestTrue(TEXT("ActiveGeoTIFFHeightSource must be valid"), Mgr->HasElevationHeightSource());

			// Load up to 64 marked chunk coordinates and verify geometry
			int32 SuccessfullyLoadedChunks = 0;
			float MinObservedHeight = 1.0f;
			float MaxObservedHeight = 0.0f;
			int32 TestedCount = 0;

			for (const FIntPoint& Coord : Mgr->GetImportedChunkCoords())
			{
				if (++TestedCount > 64) break;
				Mgr->LoadOrSpawnChunk(Coord);
				AGRTChunk* Chunk = Mgr->GetChunkAtCoord(Coord);
				if (Chunk)
				{
					++SuccessfullyLoadedChunks;
					for (float H : Chunk->GetHeightBuffer())
					{
						MinObservedHeight = FMath::Min(MinObservedHeight, H);
						MaxObservedHeight = FMath::Max(MaxObservedHeight, H);
					}
				}
			}

			AddInfo(FString::Printf(TEXT("Tested %d chunks across %d marked coords. Observed height band: [%.4f, %.4f]"),
				SuccessfullyLoadedChunks, Mgr->GetImportedChunkCoords().Num(), MinObservedHeight, MaxObservedHeight));

			TestTrue(TEXT("Marked chunks should be loadable"), SuccessfullyLoadedChunks > 0);
			TestTrue(TEXT("Height data should exhibit non-flat variation"), MaxObservedHeight > MinObservedHeight);
		}
	}

	return true;
}

#endif // WITH_EDITOR
