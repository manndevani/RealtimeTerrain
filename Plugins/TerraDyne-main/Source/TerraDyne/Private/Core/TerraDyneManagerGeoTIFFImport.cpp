// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// ATerraDyneManager -- GeoTIFF (Imported world mode) import.
//
// Fifth translation unit split out of TerraDyneManager.cpp, same convention as
// TerraDyneManagerLandscapeImport.cpp/Persistence.cpp/Population.cpp/Undo.cpp: same class, same
// members, only the file boundary is new.
//
// Unlike the Landscape importer this is NOT WITH_EDITOR-gated -- Imported mode has to work in a
// packaged build, driven by the runtime file-picker widget. What lives here is intentionally
// small: decode the file, resolve/recentre the level's CesiumGeoreference, mark which chunk
// coordinates the DEM covers, and disable outskirts/edge growth so nothing procedural grows past
// its footprint (the "hard edge" decision). It does NOT eagerly populate any chunk -- that is
// LoadOrSpawnChunk's job (see TerraDyneManager.cpp), so a DEM of any size behaves the same whether
// it covers 4 chunks or 4000: this function only ever does O(footprint-in-chunks) work, streaming
// does the O(vertices) work lazily as the player actually reaches each chunk.

#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneManagerInternal.h"
#include "World/TerraDyneChunk.h"
#include "IO/TerraDyneGeoTIFFReader.h"
#include "IO/TerraDyneGeoTIFFHeightSource.h"
#include "TerraDyneModule.h"

void ATerraDyneManager::ImportGeoTIFF(const FString& FilePath)
{
	ImportGeoTIFFInternal(FilePath, GeoTIFFImportOptions);

	// Unlike BeginPlay's own fresh-import branch (which runs this same sequence itself right after
	// its whole if/else chain), a call arriving here directly -- e.g. from the runtime file-picker
	// widget, well after BeginPlay has already finished -- has nobody else to do this for it.
	RebuildChunkMap();
	ApplyMaterialToActiveChunks();
	RefreshPopulationForLoadedChunks();
}

void ATerraDyneManager::ImportGeoTIFFInternal(const FString& FilePath, const FTerraDyneGeoTIFFImportOptions& Options)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne: ImportGeoTIFF called on a non-authority instance; ignored (chunks are server-spawned only)."));
		return;
	}

	if (FilePath.IsEmpty())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne: ImportGeoTIFF called with an empty file path."));
		return;
	}

	FTerraDyneGeoTIFFRaster Raster;
	FString ReadError;
	if (!FTerraDyneGeoTIFFReader::LoadFromFile(FilePath, Raster, ReadError))
	{
		UE_LOG(LogTerraDyne, Error, TEXT("TerraDyne: Failed to read GeoTIFF '%s': %s"), *FilePath, *ReadError);
		return;
	}

	TUniquePtr<FTerraDyneGeoTIFFHeightSource> NewSource = MakeUnique<FTerraDyneGeoTIFFHeightSource>();
	FString InitError;
	if (!NewSource->Initialize(MoveTemp(Raster), GetWorld(), Options.bEstablishGeoreferenceOrigin, Options.HeadroomFraction, Options.OverrideZScale, InitError))
	{
		UE_LOG(LogTerraDyne, Error, TEXT("TerraDyne: Failed to georeference GeoTIFF '%s': %s"), *FilePath, *InitError);
		return;
	}

	if (Options.bClearExistingChunks)
	{
		TArray<FIntPoint> ExistingCoords;
		ActiveChunkMap.GetKeys(ExistingCoords);
		for (const FIntPoint& Coord : ExistingCoords)
		{
			if (ATerraDyneChunk* Chunk = ActiveChunkMap.FindRef(Coord))
			{
				Chunk->Destroy();
			}
		}
		ActiveChunkMap.Reset();
		ImportedChunkCoords.Reset();
	}

	const FBox2D Footprint = NewSource->GetWorldFootprintXY();
	const double FootprintWidth = Footprint.Max.X - Footprint.Min.X;
	const double FootprintHeight = Footprint.Max.Y - Footprint.Min.Y;
	const double MaxDim = FMath::Max(FootprintWidth, FootprintHeight);

	// Adapt chunk size for large datasets to maintain high performance and whole-dataset top view
	if (MaxDim > 200000.0) // > 2 km
	{
		// Target ~32 chunks along the longest dimension
		const double TargetChunkSize = MaxDim / 32.0;
		// Round up to nearest 100 meters (10,000 cm)
		GlobalChunkSize = (float)(FMath::CeilToDouble(TargetChunkSize / 10000.0) * 10000.0);
	}
	else
	{
		GlobalChunkSize = 10000.0f; // Default 100m for local/small datasets
	}

	const FIntPoint MinCoord = WorldToChunkCoord(FVector(Footprint.Min.X, Footprint.Min.Y, 0.0));
	const FIntPoint MaxCoord = WorldToChunkCoord(FVector(Footprint.Max.X, Footprint.Max.Y, 0.0));
	for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; ++Y)
	{
		for (int32 X = MinCoord.X; X <= MaxCoord.X; ++X)
		{
			ImportedChunkCoords.Add(FIntPoint(X, Y));
		}
	}

	if (Options.bDisableOutskirtsGrowth)
	{
		// Hard edge, by design: an imported DEM has its own real elevation range, almost never
		// matching procedural's fixed ZScale band, so growing procedural terrain past the DEM's
		// footprint would produce a near-guaranteed visible seam. Simplest correct fix is to not
		// grow past the footprint at all.
		ProceduralWorldSettings.bEnableSeededOutskirts = false;
		ProceduralWorldSettings.bEnableInfiniteEdgeGrowth = false;
	}

	WorldOriginKind = ETerraDyneChunkOrigin::ImportedGeoTIFF;
	GeoTIFFImportPath = FilePath;
	GeoTIFFImportState.bWasImportedFromGeoTIFF = true;
	GeoTIFFImportState.SourceFilePath = FilePath;
	GeoTIFFImportState.WorldFootprintMin = Footprint.Min;
	GeoTIFFImportState.WorldFootprintMax = Footprint.Max;
	GeoTIFFImportState.ResolvedZScale = NewSource->GetZScale();
	GeoTIFFImportState.ImportedAtIso8601 = FDateTime::Now().ToIso8601();

	// Stays resident for the life of the world: chunks can stream in long after this call returns,
	// far from wherever the player started, and LoadOrSpawnChunk needs a live source to sample.
	ActiveGeoTIFFHeightSource = MoveTemp(NewSource);

	// Queue all chunks across the entire GeoTIFF footprint so the full dataset is loaded and visible
	for (const FIntPoint& Coord : ImportedChunkCoords)
	{
		if (!ActiveChunkMap.Contains(Coord) && !PendingLoadQueue.Contains(Coord))
		{
			PendingLoadQueue.Add(Coord);
		}
	}

	// The world has now been decided, so release the streaming gate BeginPlay latched while waiting
	// for the player to choose (see bAwaitingWorldModeChoice). Until this runs, streaming spawns
	// nothing at all -- which is exactly what keeps an undecided world empty, and equally what would
	// keep an imported one empty if this were forgotten.
	MarkWorldModeChosen();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: Imported GeoTIFF '%s' -- %d chunk coordinate(s) marked and queued for macro loading, ZScale=%.0f"),
		*FilePath, ImportedChunkCoords.Num(), GeoTIFFImportState.ResolvedZScale);
}

bool ATerraDyneManager::TryPopulateChunkFromGeoTIFF(ATerraDyneChunk* Chunk, FIntPoint Coord)
{
	if (!Chunk || !ActiveGeoTIFFHeightSource.IsValid())
	{
		return false;
	}

	const int32 Resolution = Chunk->Resolution;
	const int32 NumSamples = Resolution * Resolution;
	if (Resolution < 2 || NumSamples <= 0)
	{
		return false;
	}

	const FVector ChunkLocation = Chunk->GetActorLocation();
	TArray<float> Sampled;
	Sampled.SetNumZeroed(NumSamples);

	// Which vertices actually resolved. A chunk straddling the DEM's edge, or covering a NoData
	// region, gets holes -- and those must not silently become zeros: zero is the very bottom of the
	// normalised band, so an unresolved vertex would render as a sheer cliff dropping the full
	// elevation range. They are filled from the chunk's own valid samples instead.
	TArray<bool> Resolved;
	Resolved.Init(false, NumSamples);

	double ValidSum = 0.0;
	int32 ValidCount = 0;

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			// Same per-vertex layout the procedural path uses (ATerraDyneChunk::Initialize), but in
			// true absolute Unreal world space -- Cesium's georeference has no notion of "relative to
			// the TerraDyne manager actor", unlike the procedural noise sampler.
			const double WorldX = ChunkLocation.X + ((double(X) / (Resolution - 1)) - 0.5) * Chunk->WorldSize;
			const double WorldY = ChunkLocation.Y + ((double(Y) / (Resolution - 1)) - 0.5) * Chunk->WorldSize;

			const int32 Index = Y * Resolution + X;
			float Normalized01 = 0.0f;
			if (ActiveGeoTIFFHeightSource->TrySampleWorldXY(WorldX, WorldY, Normalized01))
			{
				Sampled[Index] = Normalized01;
				Resolved[Index] = true;
				ValidSum += Normalized01;
				++ValidCount;
			}
		}
	}

	if (ValidCount == 0)
	{
		// Nothing of this chunk is inside the DEM (or it is entirely NoData). Leave it to the
		// procedural path rather than emitting a flat plate at the bottom of the band.
		return false;
	}

	if (ValidCount < NumSamples)
	{
		// Flat fill at the chunk's own mean rather than zero: it keeps the unresolved margin level
		// with the terrain beside it, so the DEM edge reads as terrain running out rather than as a
		// cliff. Anything smarter (inpainting from the boundary) is a bigger change than this is
		// worth until real DEM edges are actually being looked at.
		const float FillValue = (float)(ValidSum / ValidCount);
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			if (!Resolved[Index])
			{
				Sampled[Index] = FillValue;
			}
		}
	}

	const double ChunkWorldZ = ActiveGeoTIFFHeightSource->GetChunkWorldZOffset();
	const float ResolvedZScale = ActiveGeoTIFFHeightSource->GetZScale();

	Chunk->PopulateFromDEM(
		MoveTemp(Sampled),
		ResolvedZScale,
		ChunkWorldZ,
		/*bGenerateCollision=*/true,
		GeoTIFFImportState.WorldFootprintMin,
		GeoTIFFImportState.WorldFootprintMax,
		ActiveGeoTIFFVisualOverlayPixels,
		ActiveGeoTIFFVisualOverlayWidth,
		ActiveGeoTIFFVisualOverlayHeight);

	if (ActiveGeoTIFFVisualOverlayTexture)
	{
		Chunk->SetOverlayTexture(ActiveGeoTIFFVisualOverlayTexture);
	}

	return true;
}

void ATerraDyneManager::OnRep_GeoTIFFImportState()
{
	OnRep_FrameworkConfig();
}

bool ATerraDyneManager::HasElevationHeightSource() const
{
	return ActiveGeoTIFFHeightSource.IsValid();
}

//======================================================================================
// Multi-layer raster import
//
// An elevation DEM plus the products derived from it (hillshade, colour relief, slope,
// aspect, roughness) describe the same ground and only mean anything together. The rules
// enforced here exist to keep that true: elevation first because it defines the reference
// grid, and everything afterwards checked against that grid so two unrelated datasets can
// never end up combined.
//======================================================================================

bool ATerraDyneManager::IsRasterLayerImported(ETerraDyneRasterLayer Layer) const
{
	const int32 Index = (int32)Layer;
	return RasterLayerSlots.IsValidIndex(Index) && RasterLayerSlots[Index].bImported;
}

bool ATerraDyneManager::IsRasterLayerLoadedInScene(ETerraDyneRasterLayer Layer) const
{
	const int32 Index = (int32)Layer;
	return RasterLayerSlots.IsValidIndex(Index) && RasterLayerSlots[Index].bLoadedIntoScene;
}

float ATerraDyneManager::GetGeoTIFFLoadProgress(int32& OutLoaded, int32& OutTotal) const
{
	OutTotal = ImportedChunkCoords.Num();
	if (OutTotal <= 0)
	{
		OutLoaded = 0;
		return 1.0f;
	}

	OutLoaded = 0;
	for (const FIntPoint& Coord : ImportedChunkCoords)
	{
		if (ActiveChunkMap.Contains(Coord))
		{
			OutLoaded++;
		}
	}
	return (float)OutLoaded / (float)OutTotal;
}

bool ATerraDyneManager::LoadSpecificRasterLayer(ETerraDyneRasterLayer Layer, FString& OutError)
{
	OutError.Reset();
	const int32 Index = (int32)Layer;
	if (!RasterLayerSlots.IsValidIndex(Index) || !RasterLayerSlots[Index].bImported)
	{
		OutError = FString::Printf(TEXT("Layer %s has not been imported yet."), *TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
		return false;
	}

	if (TerraDyneRasterLayers::IsElevation(Layer))
	{
		return LoadRasterLayersIntoScene(OutError);
	}

	if (!HasElevationLayer() || !IsRasterLayerLoadedInScene(ETerraDyneRasterLayer::Elevation))
	{
		OutError = TEXT("Cannot load overlay layer before Elevation (DEM) is loaded into the scene.");
		return false;
	}

	// Decode GeoTIFF visual layer into pixel buffer and GPU texture
	FString DecodeErr;
	if (!FTerraDyneGeoTIFFReader::DecodeRasterPixels(
		RasterLayerSlots[Index].SourceFilePath,
		4096,
		ActiveGeoTIFFVisualOverlayPixels,
		ActiveGeoTIFFVisualOverlayWidth,
		ActiveGeoTIFFVisualOverlayHeight,
		DecodeErr))
	{
		OutError = FString::Printf(TEXT("Failed to decode visual layer %s: %s"),
			*TerraDyneRasterLayers::GetDisplayName(Layer).ToString(), *DecodeErr);
		return false;
	}

	ActiveGeoTIFFVisualOverlayTexture = FTerraDyneGeoTIFFReader::DecodeRasterToTexture(
		RasterLayerSlots[Index].SourceFilePath,
		4096,
		DecodeErr);

	ActiveVisualOverlayLayer = Layer;
	ActiveVisualOverlayName = TerraDyneRasterLayers::GetDisplayName(Layer).ToString();

	// Reset any other visual overlay slots
	for (int32 i = 0; i < RasterLayerSlots.Num(); ++i)
	{
		if (i != (int32)ETerraDyneRasterLayer::Elevation)
		{
			RasterLayerSlots[i].bLoadedIntoScene = (i == Index);
		}
	}

	// Apply overlay colors across all active chunks in the world
	ApplyVisualOverlayToAllChunks();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: loaded visual raster layer %s into scene"),
		*TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
	return true;
}

bool ATerraDyneManager::UnloadSpecificRasterLayer(ETerraDyneRasterLayer Layer, FString& OutError)
{
	OutError.Reset();
	const int32 Index = (int32)Layer;
	if (!RasterLayerSlots.IsValidIndex(Index))
	{
		return false;
	}

	if (TerraDyneRasterLayers::IsElevation(Layer))
	{
		// Unload terrain geometry but keep the imported slot valid
		ActiveGeoTIFFHeightSource.Reset();
		ImportedChunkCoords.Reset();
		GeoTIFFImportState = FTerraDyneGeoTIFFImportState();
		WorldOriginKind = ETerraDyneChunkOrigin::Procedural;
		ActiveGeoTIFFVisualOverlayPixels.Reset();
		ActiveGeoTIFFVisualOverlayWidth = 0;
		ActiveGeoTIFFVisualOverlayHeight = 0;
		ActiveGeoTIFFVisualOverlayTexture = nullptr;
		ActiveVisualOverlayLayer = ETerraDyneRasterLayer::Elevation;
		ActiveVisualOverlayName.Reset();

		TArray<FIntPoint> ExistingCoords;
		ActiveChunkMap.GetKeys(ExistingCoords);
		for (const FIntPoint& Coord : ExistingCoords)
		{
			if (ATerraDyneChunk* Chunk = ActiveChunkMap.FindRef(Coord))
			{
				Chunk->Destroy();
			}
		}
		ActiveChunkMap.Reset();
		if (GetWorld())
		{
			RebuildChunkMap();
		}
		bAwaitingWorldModeChoice = true;
		LastStreamingHash = 0;

		RasterLayerSlots[Index].bLoadedIntoScene = false;
		for (int32 i = 0; i < RasterLayerSlots.Num(); ++i)
		{
			if (i != Index)
			{
				RasterLayerSlots[i].bLoadedIntoScene = false;
			}
		}

		UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: unloaded Elevation (DEM) geometry from scene."));
		return true;
	}

	RasterLayerSlots[Index].bLoadedIntoScene = false;
	ActiveGeoTIFFVisualOverlayPixels.Reset();
	ActiveGeoTIFFVisualOverlayWidth = 0;
	ActiveGeoTIFFVisualOverlayHeight = 0;
	ActiveGeoTIFFVisualOverlayTexture = nullptr;
	ActiveVisualOverlayLayer = ETerraDyneRasterLayer::Elevation;
	ActiveVisualOverlayName.Reset();

	ApplyVisualOverlayToAllChunks();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: unloaded raster layer %s from scene"),
		*TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
	return true;
}

void ATerraDyneManager::ApplyVisualOverlayToAllChunks()
{
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			Pair.Value->ApplyVisualOverlay(
				ActiveGeoTIFFVisualOverlayPixels,
				ActiveGeoTIFFVisualOverlayWidth,
				ActiveGeoTIFFVisualOverlayHeight,
				GeoTIFFImportState.WorldFootprintMin,
				GeoTIFFImportState.WorldFootprintMax);

			if (ActiveGeoTIFFVisualOverlayTexture)
			{
				Pair.Value->SetOverlayTexture(ActiveGeoTIFFVisualOverlayTexture);
			}
		}
	}
}

bool ATerraDyneManager::ImportRasterLayer(ETerraDyneRasterLayer Layer, const FString& FilePath, FString& OutError)
{
	OutError.Reset();

	const int32 Index = (int32)Layer;
	if (!RasterLayerSlots.IsValidIndex(Index))
	{
		OutError = TEXT("Unknown raster layer.");
		return false;
	}

	if (FilePath.IsEmpty())
	{
		OutError = TEXT("No file selected.");
		return false;
	}

	if (!FPaths::FileExists(FilePath))
	{
		OutError = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}

	// Rule 1: elevation first. Checked before touching the file at all -- the answer does not depend
	// on what the file contains, and saying so immediately is clearer than validating a raster only
	// to reject it for a reason unrelated to itself.
	const bool bIsElevation = TerraDyneRasterLayers::IsElevation(Layer);
	if (!bIsElevation && !HasElevationLayer())
	{
		OutError = FString::Printf(
			TEXT("Import the Elevation (DEM) layer first -- %s is checked for alignment against it, so there is nothing to validate it against yet."),
			*TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
		return false;
	}

	FTerraDyneRasterInfo Info;
	FString ProbeError;
	if (!FTerraDyneGeoTIFFReader::ProbeMetadata(FilePath, Info, ProbeError))
	{
		OutError = FString::Printf(TEXT("Could not read '%s': %s"), *FPaths::GetCleanFilename(FilePath), *ProbeError);
		return false;
	}

	FString BandReason;
	if (!TerraDyneRasterLayers::IsBandCountValidFor(Layer, Info.BandCount, BandReason))
	{
		OutError = FString::Printf(TEXT("'%s' cannot be used as %s: %s."),
			*FPaths::GetCleanFilename(FilePath),
			*TerraDyneRasterLayers::GetDisplayName(Layer).ToString(),
			*BandReason);
		return false;
	}

	// Rule 2: everything must sit on the elevation raster's grid.
	if (!bIsElevation)
	{
		const FTerraDyneRasterInfo& Reference = RasterLayerSlots[(int32)ETerraDyneRasterLayer::Elevation].Info;
		FString Reason;
		if (!Reference.IsCompatibleWith(Info, Reason))
		{
			OutError = FString::Printf(
				TEXT("'%s' does not match the imported Elevation layer: %s. These look like different datasets -- both layers have to come from the same source area."),
				*FPaths::GetCleanFilename(FilePath), *Reason);
			return false;
		}
	}

	FTerraDyneRasterLayerSlot& Slot = RasterLayerSlots[Index];
	Slot.Reset();
	Slot.bImported = true;
	Slot.SourceFilePath = FilePath;
	Slot.Width = Info.Width;
	Slot.Height = Info.Height;
	Slot.BandCount = Info.BandCount;
	Slot.Info = Info;

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: imported raster layer %s from '%s' (%dx%d, %d band(s))"),
		*TerraDyneRasterLayers::GetDisplayName(Layer).ToString(), *FilePath,
		Info.Width, Info.Height, Info.BandCount);

	return true;
}

void ATerraDyneManager::UnloadRasterLayer(ETerraDyneRasterLayer Layer)
{
	const int32 Index = (int32)Layer;
	if (!RasterLayerSlots.IsValidIndex(Index) || !RasterLayerSlots[Index].bImported)
	{
		return;
	}

	if (TerraDyneRasterLayers::IsElevation(Layer))
	{
		// The reference grid is going away, so nothing that was validated against it can be trusted
		// to belong to whatever gets imported next.
		UnloadAllRasterLayers();
		return;
	}

	RasterLayerSlots[Index].Reset();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: unloaded raster layer %s"),
		*TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
}

void ATerraDyneManager::UnloadAllRasterLayers()
{
	for (FTerraDyneRasterLayerSlot& Slot : RasterLayerSlots)
	{
		Slot.Reset();
	}

	// Drop the terrain built from them too, so "unload" means the world actually goes back to being
	// undecided rather than leaving imported chunks standing with no layers behind them.
	ActiveGeoTIFFHeightSource.Reset();
	ImportedChunkCoords.Reset();
	GeoTIFFImportState = FTerraDyneGeoTIFFImportState();
	GeoTIFFImportPath.Reset();
	WorldOriginKind = ETerraDyneChunkOrigin::Procedural;

	// Same teardown the import's own bClearExistingChunks path performs.
	TArray<FIntPoint> ExistingCoords;
	ActiveChunkMap.GetKeys(ExistingCoords);
	for (const FIntPoint& Coord : ExistingCoords)
	{
		if (ATerraDyneChunk* Chunk = ActiveChunkMap.FindRef(Coord))
		{
			Chunk->Destroy();
		}
	}
	ActiveChunkMap.Reset();
	if (GetWorld())
	{
		RebuildChunkMap();
	}

	// Back to undecided: streaming must not immediately refill the emptied world with procedural
	// chunks, which is exactly what it would do the next tick otherwise.
	bAwaitingWorldModeChoice = true;
	LastStreamingHash = 0;

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: unloaded all raster layers and cleared imported terrain."));
}

bool ATerraDyneManager::LoadRasterLayersIntoScene(FString& OutError)
{
	OutError.Reset();

	if (!HasElevationLayer())
	{
		OutError = TEXT("Nothing to load: import an Elevation (DEM) layer first.");
		return false;
	}

	FTerraDyneRasterLayerSlot& ElevationSlot = RasterLayerSlots[(int32)ETerraDyneRasterLayer::Elevation];

	// This is the expensive step the import deliberately deferred: the full decode plus terrain build.
	ImportGeoTIFF(ElevationSlot.SourceFilePath);

	if (!ActiveGeoTIFFHeightSource.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Failed to build terrain from '%s' -- see the log for the reason."),
			*FPaths::GetCleanFilename(ElevationSlot.SourceFilePath));
		return false;
	}

	ElevationSlot.bLoadedIntoScene = true;

	// Overlay layers are validated, decoded on demand, and recorded as loaded -- but draping them
	// over the terrain surface needs material work that does not exist yet, so say so plainly rather
	// than reporting success for something the user will not see.
	int32 PendingOverlays = 0;
	for (int32 i = 0; i < RasterLayerSlots.Num(); ++i)
	{
		if (i == (int32)ETerraDyneRasterLayer::Elevation) continue;
		if (RasterLayerSlots[i].bImported) ++PendingOverlays;
	}

	if (PendingOverlays > 0)
	{
		UE_LOG(LogTerraDyne, Warning,
			TEXT("TerraDyne: %d overlay layer(s) are imported and validated against the DEM, but overlay draping onto the terrain material is not implemented yet -- only elevation is visible in the scene."),
			PendingOverlays);
	}

	return true;
}

bool ATerraDyneManager::AddDynamicRasterLayer(
	const FString& FilePath,
	FTerraDyneDynamicRasterLayer& OutLayer,
	FString& OutError)
{
	OutError.Reset();

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		OutError = TEXT("File does not exist or path is invalid.");
		return false;
	}

	// 1. Check for duplicates against primary Elevation DEM
	if (RasterLayerSlots.IsValidIndex((int32)ETerraDyneRasterLayer::Elevation) &&
		RasterLayerSlots[(int32)ETerraDyneRasterLayer::Elevation].bImported &&
		FPaths::IsSamePath(RasterLayerSlots[(int32)ETerraDyneRasterLayer::Elevation].SourceFilePath, FilePath))
	{
		OutError = FString::Printf(
			TEXT("Duplicate Layer: '%s' is already imported as the master Elevation (DEM)."),
			*FPaths::GetCleanFilename(FilePath));
		return false;
	}

	// 2. Check for duplicates in DynamicRasterLayers
	for (const FTerraDyneDynamicRasterLayer& Existing : DynamicRasterLayers)
	{
		if (FPaths::IsSamePath(Existing.SourceFilePath, FilePath))
		{
			OutError = FString::Printf(
				TEXT("Duplicate Layer: '%s' is already in your dynamic layer stack (%s)."),
				*FPaths::GetCleanFilename(FilePath), *Existing.DisplayName);
			return false;
		}
	}

	// 3. Auto-detect raster type and metadata
	if (!TerraDyneRasterLayers::AutoDetectRasterType(
		FilePath,
		OutLayer.LayerType,
		OutLayer.DisplayName,
		OutLayer.Info,
		OutError))
	{
		return false;
	}

	// 4. Validate georeferencing against master DEM if present
	if (HasElevationLayer())
	{
		const FTerraDyneRasterInfo& DemInfo = RasterLayerSlots[(int32)ETerraDyneRasterLayer::Elevation].Info;
		if (DemInfo.Width > 0 && DemInfo.Height > 0)
		{
			// Check CRS compatibility if both declare projection
			if (DemInfo.bIsUTM && OutLayer.Info.bIsUTM && DemInfo.UTMZone != OutLayer.Info.UTMZone)
			{
				UE_LOG(LogTerraDyne, Warning,
					TEXT("TerraDyne: Layer '%s' UTM Zone (%d) differs from DEM UTM Zone (%d) -- proceeding with local footprint mapping."),
					*FPaths::GetCleanFilename(FilePath), OutLayer.Info.UTMZone, DemInfo.UTMZone);
			}
		}
	}

	OutLayer.LayerId = FGuid::NewGuid();
	OutLayer.SourceFilePath = FilePath;
	OutLayer.Width = OutLayer.Info.Width;
	OutLayer.Height = OutLayer.Info.Height;
	OutLayer.BandCount = OutLayer.Info.BandCount;
	OutLayer.bIsActiveInScene = false;

	DynamicRasterLayers.Add(OutLayer);

	UE_LOG(LogTerraDyne, Log,
		TEXT("TerraDyne: Added dynamic raster layer '%s' as [%s] (%dx%d, %d bands)"),
		*FilePath, *OutLayer.DisplayName, OutLayer.Width, OutLayer.Height, OutLayer.BandCount);

	return true;
}

bool ATerraDyneManager::RemoveDynamicRasterLayer(int32 LayerIndex)
{
	if (!DynamicRasterLayers.IsValidIndex(LayerIndex))
	{
		return false;
	}

	if (DynamicRasterLayers[LayerIndex].bIsActiveInScene)
	{
		UnloadDynamicRasterLayer(LayerIndex);
	}

	DynamicRasterLayers.RemoveAt(LayerIndex);
	return true;
}

void ATerraDyneManager::ClearAllDynamicRasterLayers()
{
	for (int32 i = 0; i < DynamicRasterLayers.Num(); ++i)
	{
		if (DynamicRasterLayers[i].bIsActiveInScene)
		{
			UnloadDynamicRasterLayer(i);
		}
	}
	DynamicRasterLayers.Reset();
}

bool ATerraDyneManager::LoadDynamicRasterLayer(int32 LayerIndex, FString& OutError)
{
	OutError.Reset();

	if (!DynamicRasterLayers.IsValidIndex(LayerIndex))
	{
		OutError = TEXT("Invalid dynamic layer index.");
		return false;
	}

	if (!HasElevationLayer() || !IsRasterLayerLoadedInScene(ETerraDyneRasterLayer::Elevation))
	{
		OutError = TEXT("Cannot load overlay layer before Elevation (DEM) is loaded into the scene.");
		return false;
	}

	const FTerraDyneDynamicRasterLayer& Layer = DynamicRasterLayers[LayerIndex];

	// Decode GeoTIFF visual layer into pixel buffer and GPU texture
	FString DecodeErr;
	if (!FTerraDyneGeoTIFFReader::DecodeRasterPixels(
		Layer.SourceFilePath,
		4096,
		ActiveGeoTIFFVisualOverlayPixels,
		ActiveGeoTIFFVisualOverlayWidth,
		ActiveGeoTIFFVisualOverlayHeight,
		DecodeErr))
	{
		OutError = FString::Printf(TEXT("Failed to decode layer '%s': %s"), *Layer.DisplayName, *DecodeErr);
		return false;
	}

	ActiveGeoTIFFVisualOverlayTexture = FTerraDyneGeoTIFFReader::DecodeRasterToTexture(
		Layer.SourceFilePath,
		4096,
		DecodeErr);

	ActiveVisualOverlayLayer = Layer.LayerType;
	ActiveVisualOverlayName = Layer.DisplayName;

	// Reset any static visual overlay slots
	for (int32 i = 0; i < RasterLayerSlots.Num(); ++i)
	{
		if (i != (int32)ETerraDyneRasterLayer::Elevation)
		{
			RasterLayerSlots[i].bLoadedIntoScene = false;
		}
	}

	// Update active state in dynamic list
	for (int32 i = 0; i < DynamicRasterLayers.Num(); ++i)
	{
		DynamicRasterLayers[i].bIsActiveInScene = (i == LayerIndex);
	}

	// Apply overlay colors across all active chunks in the world
	ApplyVisualOverlayToAllChunks();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: loaded dynamic raster layer '%s' into scene"), *Layer.DisplayName);
	return true;
}

void ATerraDyneManager::UnloadDynamicRasterLayer(int32 LayerIndex)
{
	if (DynamicRasterLayers.IsValidIndex(LayerIndex))
	{
		DynamicRasterLayers[LayerIndex].bIsActiveInScene = false;
	}

	ActiveGeoTIFFVisualOverlayPixels.Reset();
	ActiveGeoTIFFVisualOverlayWidth = 0;
	ActiveGeoTIFFVisualOverlayHeight = 0;
	ActiveGeoTIFFVisualOverlayTexture = nullptr;
	ActiveVisualOverlayLayer = ETerraDyneRasterLayer::Elevation;
	ActiveVisualOverlayName.Reset();

	ApplyVisualOverlayToAllChunks();
}
