// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/GRTManager.h"
#include "GRTModule.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Components/SplineComponent.h"

#include "Cesium3DTileset.h"
#include "CesiumUrlTemplateRasterOverlay.h"
#include "CesiumCartographicPolygon.h"
#include "CesiumPolygonRasterOverlay.h"
#include "CesiumGeoreference.h"

namespace
{
	/**
	 * Builds a file:/// URL from an absolute Windows path, matching the "file:///" prefix
	 * UnrealAssetAccessor::isFile checks for (three slashes, then the drive letter).
	 */
	FString ToFileUrl(const FString& AbsolutePath)
	{
		FString Normalized = AbsolutePath;
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!Normalized.StartsWith(TEXT("/")))
		{
			Normalized = TEXT("/") + Normalized;
		}
		return TEXT("file://") + Normalized;
	}

	/** Scans PyramidRoot's immediate subdirectories for numeric zoom-level folder names. */
	void DetectZoomRange(const FString& PyramidRoot, int32& OutMinZoom, int32& OutMaxZoom)
	{
		OutMinZoom = TNumericLimits<int32>::Max();
		OutMaxZoom = -1;

		FPlatformFileManager::Get().GetPlatformFile().IterateDirectory(
			*PyramidRoot,
			[&OutMinZoom, &OutMaxZoom](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
			{
				if (bIsDirectory)
				{
					const FString Name = FPaths::GetCleanFilename(FilenameOrDirectory);
					if (Name.IsNumeric())
					{
						const int32 Zoom = FCString::Atoi(*Name);
						OutMinZoom = FMath::Min(OutMinZoom, Zoom);
						OutMaxZoom = FMath::Max(OutMaxZoom, Zoom);
					}
				}
				return true;
			});

		if (OutMaxZoom < 0)
		{
			OutMinZoom = 0;
		}
	}

	struct FTilePyramidIndexBounds
	{
		int32 MinX = TNumericLimits<int32>::Max();
		int32 MaxX = -1;
		int32 MinY = TNumericLimits<int32>::Max();
		int32 MaxY = -1;

		bool IsValid() const { return MaxX >= 0 && MaxY >= 0; }
	};

	/** Scans one zoom level's tile index range: {PyramidRoot}/{Zoom}/{x}/{y}{FileExtension}. */
	FTilePyramidIndexBounds ScanZoomLevelTileIndexBounds(const FString& PyramidRoot, int32 Zoom, const TCHAR* FileExtension)
	{
		FTilePyramidIndexBounds Bounds;
		const FString ZoomDir = FPaths::Combine(PyramidRoot, FString::FromInt(Zoom));

		FPlatformFileManager::Get().GetPlatformFile().IterateDirectory(
			*ZoomDir,
			[&Bounds, FileExtension](const TCHAR* XPath, bool bIsDirectory) -> bool
			{
				if (!bIsDirectory)
				{
					return true;
				}
				const FString XName = FPaths::GetCleanFilename(XPath);
				if (!XName.IsNumeric())
				{
					return true;
				}
				const int32 X = FCString::Atoi(*XName);

				FPlatformFileManager::Get().GetPlatformFile().IterateDirectory(
					XPath,
					[&Bounds, X, FileExtension](const TCHAR* FilePath, bool bInnerIsDirectory) -> bool
					{
						if (bInnerIsDirectory)
						{
							return true;
						}
						const FString FileName = FPaths::GetCleanFilename(FilePath);
						if (!FileName.EndsWith(FileExtension))
						{
							return true;
						}
						const FString YName = FPaths::GetBaseFilename(FileName);
						if (!YName.IsNumeric())
						{
							return true;
						}
						const int32 Y = FCString::Atoi(*YName);
						Bounds.MinX = FMath::Min(Bounds.MinX, X);
						Bounds.MaxX = FMath::Max(Bounds.MaxX, X);
						Bounds.MinY = FMath::Min(Bounds.MinY, Y);
						Bounds.MaxY = FMath::Max(Bounds.MaxY, Y);
						return true;
					});

				return true;
			});

		return Bounds;
	}

	/**
	 * Geographic centre/span of a TMS-scheme quantized-mesh pyramid's deepest zoom level. Level 0 is
	 * one 2-wide x 1-tall tile pair covering the whole globe (EPSG:4326 geographic), so both axes
	 * share the same angular tile size at any zoom: 360 / 2^(Zoom+1) degrees -- unlike WebMercator,
	 * no separate latitude conversion is needed.
	 */
	bool ComputeQuantizedMeshGeographicBounds(const FString& PyramidRoot, FVector2D& OutCenterLonLat, double& OutSpanDegrees)
	{
		int32 MinZoom = 0, MaxZoom = -1;
		DetectZoomRange(PyramidRoot, MinZoom, MaxZoom);
		if (MaxZoom < 0)
		{
			return false;
		}

		const FTilePyramidIndexBounds Index = ScanZoomLevelTileIndexBounds(PyramidRoot, MaxZoom, TEXT(".terrain"));
		if (!Index.IsValid())
		{
			return false;
		}

		const double Step = 360.0 / FMath::Pow(2.0, MaxZoom + 1);
		const double LonMin = -180.0 + Index.MinX * Step;
		const double LonMax = -180.0 + (Index.MaxX + 1) * Step;
		const double LatMin = -90.0 + Index.MinY * Step;
		const double LatMax = -90.0 + (Index.MaxY + 1) * Step;

		OutCenterLonLat = FVector2D((LonMin + LonMax) * 0.5, (LatMin + LatMax) * 0.5);
		OutSpanDegrees = FMath::Max(LonMax - LonMin, LatMax - LatMin);
		return true;
	}

	double WebMercatorTileRowToLatitude(int32 Zoom, int32 Row)
	{
		const double N = FMath::Pow(2.0, Zoom);
		const double Rad = PI - 2.0 * PI * (double)Row / N;
		return FMath::RadiansToDegrees(FMath::Atan(FMath::Sinh(Rad)));
	}

	/** Geographic centre/span of an XYZ WebMercator imagery pyramid's DeepestZoom level. */
	bool ComputeWebMercatorGeographicBounds(const FString& PyramidRoot, int32 DeepestZoom, FVector2D& OutCenterLonLat, double& OutSpanDegrees)
	{
		const FTilePyramidIndexBounds Index = ScanZoomLevelTileIndexBounds(PyramidRoot, DeepestZoom, TEXT(".png"));
		if (!Index.IsValid())
		{
			return false;
		}

		const double N = FMath::Pow(2.0, DeepestZoom);
		const double LonMin = (double)Index.MinX / N * 360.0 - 180.0;
		const double LonMax = (double)(Index.MaxX + 1) / N * 360.0 - 180.0;
		// XYZ row index increases southward, so the smaller row is the pyramid's NORTHERN edge.
		const double LatMax = WebMercatorTileRowToLatitude(DeepestZoom, Index.MinY);
		const double LatMin = WebMercatorTileRowToLatitude(DeepestZoom, Index.MaxY + 1);

		OutCenterLonLat = FVector2D((LonMin + LonMax) * 0.5, (LatMin + LatMax) * 0.5);
		OutSpanDegrees = FMath::Max(LonMax - LonMin, LatMax - LatMin);
		return true;
	}

	/** The four corners of a rectangle footprint, centre +/- half the span in each axis. */
	TArray<FVector2D> FootprintCornersLonLat(const FVector2D& CenterLonLat, double SpanDegrees)
	{
		const double Half = FMath::Max(SpanDegrees, 0.0001) * 0.5;
		return {
			FVector2D(CenterLonLat.X - Half, CenterLonLat.Y - Half),
			FVector2D(CenterLonLat.X + Half, CenterLonLat.Y - Half),
			FVector2D(CenterLonLat.X + Half, CenterLonLat.Y + Half),
			FVector2D(CenterLonLat.X - Half, CenterLonLat.Y + Half),
		};
	}

	/** True if Overlay already clips against ClipPolygon (TSoftObjectPtr doesn't compare cleanly against a raw pointer via Contains). */
	bool ClipsAgainst(const UCesiumPolygonRasterOverlay* Overlay, const ACesiumCartographicPolygon* ClipPolygon)
	{
		for (const TSoftObjectPtr<ACesiumCartographicPolygon>& P : Overlay->Polygons)
		{
			if (P.Get() == ClipPolygon)
			{
				return true;
			}
		}
		return false;
	}
}

bool AGRTManager::ImportCesiumTerrainTileset(const FString& PyramidFolder, FString& OutError, FVector2D& OutCenterLonLat, double& OutSpanDegrees)
{
	OutError.Reset();
	OutCenterLonLat = FVector2D::ZeroVector;
	OutSpanDegrees = 0.0;

	// Same "is this actually a DEM world" test SGRTPanel::GetGeoTIFFSectionVisibility uses --
	// a real-world tile pyramid has no meaningful place in a procedurally generated Sandbox world.
	if (WorldOriginKind != EGRTChunkOrigin::ImportedGeoTIFF && !HasElevationLayer())
	{
		OutError = TEXT("Tiled Cesium terrain import is only available in Imported (DEM) worlds, not Sandbox.");
		return false;
	}

	const FString LayerJsonPath = FPaths::Combine(PyramidFolder, TEXT("layer.json"));
	if (!FPaths::FileExists(LayerJsonPath))
	{
		OutError = FString::Printf(TEXT("No layer.json found in '%s' -- expected a Cesium quantized-mesh tileset root."), *PyramidFolder);
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		OutError = TEXT("No world to spawn the tileset in.");
		return false;
	}

	const FString Url = ToFileUrl(FPaths::ConvertRelativePathToFull(LayerJsonPath));

	if (!ActiveTiledCesiumTileset)
	{
		// Deferred spawn: ACesium3DTileset's own OnConstruction (which fires as part of a plain
		// SpawnActor, before this function gets to call SetTilesetSource/SetUrl) calls LoadTileset()
		// immediately using whatever TilesetSource the class default left it at -- FromCesiumIon,
		// which touches CesiumIonServer. Setting the real source/URL here, before FinishSpawning(),
		// means OnConstruction never sees FromCesiumIon at all -- this is what actually keeps the
		// local-pyramid path offline, not just calling SetTilesetSource() after a normal spawn.
		ACesium3DTileset* NewTileset = World->SpawnActorDeferred<ACesium3DTileset>(ACesium3DTileset::StaticClass(), FTransform::Identity);
		if (!NewTileset)
		{
			OutError = TEXT("Failed to spawn ACesium3DTileset.");
			return false;
		}
		NewTileset->SetTilesetSource(ETilesetSource::FromUrl);
		NewTileset->SetUrl(Url);
		NewTileset->FinishSpawning(FTransform::Identity);
		ActiveTiledCesiumTileset = NewTileset;
	}
	else
	{
		ActiveTiledCesiumTileset->SetTilesetSource(ETilesetSource::FromUrl);
		ActiveTiledCesiumTileset->SetUrl(Url);

		// SetUrl (when the URL actually changes) destroys the tileset's internal cesium-native
		// Tileset object and rebuilds a new one -- but any raster overlay component already
		// attached via AddToTileset() keeps its old internal handle, which now refers to nothing.
		// Without this, re-pointing terrain to a new location silently orphans a previously-imported
		// imagery overlay: it stays a live component with the right properties, but stops actually
		// draping anything, since it's still registered against the tileset object that just got
		// destroyed. Refresh() removes (a no-op against the stale handle, harmless) and re-adds
		// against whatever tileset FindTileset() resolves to now -- the freshly rebuilt one.
		if (ActiveTiledCesiumOverlay)
		{
			ActiveTiledCesiumOverlay->Refresh();
		}
	}

	ApplyTiledCesiumHeightOffset();

	if (ComputeQuantizedMeshGeographicBounds(PyramidFolder, OutCenterLonLat, OutSpanDegrees))
	{
		LastTiledImportFootprintCenterLonLat = OutCenterLonLat;
		LastTiledImportFootprintSpanDegrees = OutSpanDegrees;
		UE_LOG(LogGRT, Log, TEXT("GRT: Cesium terrain tileset pointed at %s (footprint centre %.5f,%.5f, span %.4f deg)"),
			*Url, OutCenterLonLat.X, OutCenterLonLat.Y, OutSpanDegrees);
	}
	else
	{
		LastTiledImportFootprintSpanDegrees = 0.0;
		UE_LOG(LogGRT, Log, TEXT("GRT: Cesium terrain tileset pointed at %s (footprint undetermined)"), *Url);
	}

	RefreshTiledCesiumBackdropCutout();
	return true;
}

bool AGRTManager::ImportCesiumImageryOverlay(const FString& PyramidFolder, FString& OutError, FVector2D& OutCenterLonLat, double& OutSpanDegrees)
{
	OutError.Reset();
	OutCenterLonLat = FVector2D::ZeroVector;
	OutSpanDegrees = 0.0;

	if (WorldOriginKind != EGRTChunkOrigin::ImportedGeoTIFF && !HasElevationLayer())
	{
		OutError = TEXT("Tiled Cesium imagery import is only available in Imported (DEM) worlds, not Sandbox.");
		return false;
	}

	int32 MinZoom = 0, MaxZoom = -1;
	DetectZoomRange(PyramidFolder, MinZoom, MaxZoom);
	if (MaxZoom < 0)
	{
		OutError = FString::Printf(TEXT("No numeric zoom-level subfolders found under '%s' -- expected a {z}/{x}/{y}.png tile pyramid."), *PyramidFolder);
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		OutError = TEXT("No world to spawn the overlay in.");
		return false;
	}

	if (!ActiveTiledCesiumTileset)
	{
		// No terrain tileset imported yet -- spawn a bare ellipsoid tileset so the overlay has
		// something to drape onto. Deferred spawn for the same reason as ImportCesiumTerrainTileset:
		// set FromEllipsoid before FinishSpawning() so OnConstruction never runs against the class's
		// FromCesiumIon default.
		ACesium3DTileset* NewTileset = World->SpawnActorDeferred<ACesium3DTileset>(ACesium3DTileset::StaticClass(), FTransform::Identity);
		if (!NewTileset)
		{
			OutError = TEXT("Failed to spawn ACesium3DTileset for the overlay.");
			return false;
		}
		NewTileset->SetTilesetSource(ETilesetSource::FromEllipsoid);
		NewTileset->FinishSpawning(FTransform::Identity);
		ActiveTiledCesiumTileset = NewTileset;
		ApplyTiledCesiumHeightOffset();
	}

	const bool bIsNewOverlay = !ActiveTiledCesiumOverlay;
	if (bIsNewOverlay)
	{
		ActiveTiledCesiumOverlay = NewObject<UCesiumUrlTemplateRasterOverlay>(ActiveTiledCesiumTileset);
		ActiveTiledCesiumOverlay->RegisterComponent();
		ActiveTiledCesiumTileset->AddInstanceComponent(ActiveTiledCesiumOverlay);
	}

	// {reverseY}, not {y}: per UCesiumUrlTemplateRasterOverlay::TemplateUrl's own doc comment, "{y}"
	// means TMS convention (0 = southernmost row), while "{reverseY}" means standard XYZ/slippy-map
	// convention (0 = northernmost row) -- and these locally-exported pyramids are XYZ (confirmed by
	// their own QGIS-generated viewer HTML declaring "tms: false", and by the tile-index math: a
	// disk row index R maps to the requested TMS row via (2^Zoom - 1) - R when {y} is used, which
	// exactly matched the file-not-found rows logged when this bug was still in place).
	const FString Template = ToFileUrl(FPaths::ConvertRelativePathToFull(PyramidFolder)) + TEXT("/{z}/{x}/{reverseY}.png");
	ActiveTiledCesiumOverlay->TemplateUrl = Template;
	ActiveTiledCesiumOverlay->Projection = ECesiumUrlTemplateRasterOverlayProjection::WebMercator;
	ActiveTiledCesiumOverlay->MinimumLevel = MinZoom;
	ActiveTiledCesiumOverlay->MaximumLevel = MaxZoom;
	ActiveTiledCesiumOverlay->TileWidth = 256;
	ActiveTiledCesiumOverlay->TileHeight = 256;

	if (bIsNewOverlay)
	{
		ActiveTiledCesiumOverlay->AddToTileset();
	}
	else
	{
		ActiveTiledCesiumOverlay->Refresh();
	}

	if (ComputeWebMercatorGeographicBounds(PyramidFolder, MaxZoom, OutCenterLonLat, OutSpanDegrees))
	{
		UE_LOG(LogGRT, Log, TEXT("GRT: Cesium imagery overlay pointed at %s (zoom %d-%d, footprint centre %.5f,%.5f, span %.4f deg)"),
			*Template, MinZoom, MaxZoom, OutCenterLonLat.X, OutCenterLonLat.Y, OutSpanDegrees);
	}
	else
	{
		UE_LOG(LogGRT, Log, TEXT("GRT: Cesium imagery overlay pointed at %s (zoom %d-%d, footprint undetermined)"), *Template, MinZoom, MaxZoom);
	}
	return true;
}

void AGRTManager::SetCutOutBackdropForTiledImport(bool bEnabled)
{
	bCutOutBackdropForTiledImport = bEnabled;
	RefreshTiledCesiumBackdropCutout();
}

void AGRTManager::SetTiledCesiumHeightOffset(double OffsetMeters)
{
	TiledCesiumHeightOffsetMeters = OffsetMeters;
	ApplyTiledCesiumHeightOffset();
}

void AGRTManager::ApplyTiledCesiumHeightOffset()
{
	if (ActiveTiledCesiumTileset)
	{
		// Absolute, not relative -- the tileset was spawned at FTransform::Identity, so this is a
		// straight replacement each time, not a cumulative add that would drift with repeated calls.
		ActiveTiledCesiumTileset->SetActorLocation(FVector(0.0, 0.0, TiledCesiumHeightOffsetMeters * 100.0));
	}
}

void AGRTManager::RefreshTiledCesiumBackdropCutout()
{
	UWorld* World = GetWorld();
	if (!World || !ActiveTiledCesiumTileset)
	{
		return;
	}

	if (!bCutOutBackdropForTiledImport || LastTiledImportFootprintSpanDegrees <= 0.0)
	{
		// Cutout disabled (or no footprint known yet, e.g. a bare-ellipsoid-only import) -- remove
		// any clip this manager previously attached and leave every other tileset fully intact.
		// Also un-hides anything an OLDER build's blanket-hide behaviour left hidden.
		for (TActorIterator<ACesium3DTileset> It(World); It; ++It)
		{
			ACesium3DTileset* Tileset = *It;
			if (!Tileset || Tileset == ActiveTiledCesiumTileset)
			{
				continue;
			}
			Tileset->SetActorHiddenInGame(false);

			if (!ActiveTiledCesiumClipPolygon)
			{
				continue;
			}
			TArray<UCesiumPolygonRasterOverlay*> Overlays;
			Tileset->GetComponents<UCesiumPolygonRasterOverlay>(Overlays);
			for (UCesiumPolygonRasterOverlay* Overlay : Overlays)
			{
				if (Overlay && ClipsAgainst(Overlay, ActiveTiledCesiumClipPolygon))
				{
					Overlay->RemoveFromTileset();
					Overlay->DestroyComponent();
				}
			}
		}
		return;
	}

	ACesiumGeoreference* Georeference = ACesiumGeoreference::GetDefaultGeoreference(World);
	if (!Georeference)
	{
		return;
	}

	if (!ActiveTiledCesiumClipPolygon)
	{
		ActiveTiledCesiumClipPolygon = World->SpawnActor<ACesiumCartographicPolygon>();
	}
	if (!ActiveTiledCesiumClipPolygon || !ActiveTiledCesiumClipPolygon->Polygon)
	{
		return;
	}

	TArray<FVector> WorldPoints;
	for (const FVector2D& Corner : FootprintCornersLonLat(LastTiledImportFootprintCenterLonLat, LastTiledImportFootprintSpanDegrees))
	{
		WorldPoints.Add(Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Corner.X, Corner.Y, 0.0)));
	}
	ActiveTiledCesiumClipPolygon->Polygon->SetSplinePoints(WorldPoints, ESplineCoordinateSpace::World, false);
	ActiveTiledCesiumClipPolygon->Polygon->SetClosedLoop(true, true);

	for (TActorIterator<ACesium3DTileset> It(World); It; ++It)
	{
		ACesium3DTileset* Tileset = *It;
		if (!Tileset || Tileset == ActiveTiledCesiumTileset)
		{
			continue;
		}
		Tileset->SetActorHiddenInGame(false);

		TArray<UCesiumPolygonRasterOverlay*> Overlays;
		Tileset->GetComponents<UCesiumPolygonRasterOverlay>(Overlays);
		UCesiumPolygonRasterOverlay* ClipOverlay = nullptr;
		for (UCesiumPolygonRasterOverlay* Overlay : Overlays)
		{
			if (Overlay && ClipsAgainst(Overlay, ActiveTiledCesiumClipPolygon))
			{
				ClipOverlay = Overlay;
				break;
			}
		}

		const bool bIsNewClipOverlay = (ClipOverlay == nullptr);
		if (bIsNewClipOverlay)
		{
			ClipOverlay = NewObject<UCesiumPolygonRasterOverlay>(Tileset);
			ClipOverlay->Polygons.Add(ActiveTiledCesiumClipPolygon);
			ClipOverlay->RegisterComponent();
			Tileset->AddInstanceComponent(ClipOverlay);
			ClipOverlay->AddToTileset();
		}
		else
		{
			ClipOverlay->Refresh();
		}
	}
}
