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

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne: Imported GeoTIFF '%s' -- %d chunk coordinate(s) marked, ZScale=%.0f"),
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

	bool bAnySampled = false;
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			// Same per-vertex layout the procedural path uses (ATerraDyneChunk::Initialize), but in
			// true absolute Unreal world space -- Cesium's georeference has no notion of "relative to
			// the TerraDyne manager actor", unlike the procedural noise sampler.
			const double WorldX = ChunkLocation.X + ((double(X) / (Resolution - 1)) - 0.5) * Chunk->WorldSize;
			const double WorldY = ChunkLocation.Y + ((double(Y) / (Resolution - 1)) - 0.5) * Chunk->WorldSize;

			float Normalized01 = 0.0f;
			if (ActiveGeoTIFFHeightSource->TrySampleWorldXY(WorldX, WorldY, Normalized01))
			{
				bAnySampled = true;
			}
			Sampled[Y * Resolution + X] = Normalized01;
		}
	}

	if (!bAnySampled)
	{
		return false;
	}

	Chunk->ZScale = ActiveGeoTIFFHeightSource->GetZScale();
	Chunk->BaseBuffer = MoveTemp(Sampled);
	Chunk->SculptBuffer.Init(0.0f, NumSamples);
	Chunk->DetailBuffer.Init(0.0f, NumSamples);
	Chunk->RecomputeHeightFromLayers();
	Chunk->RebuildPhysicsMesh();
	Chunk->UploadCPUHeightTexture();

	return true;
}

void ATerraDyneManager::OnRep_GeoTIFFImportState()
{
	OnRep_FrameworkConfig();
}
