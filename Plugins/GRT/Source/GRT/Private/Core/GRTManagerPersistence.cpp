// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// AGRTManager -- persistence.
//
// Third translation unit split out of GRTManager.cpp. Same members, same class, same API;
// only the file boundary is new. See GRTManagerPopulation.cpp for the first.
//
// What lives here is everything that moves world state to and from disk, at two different
// granularities:
//
//   whole world   BuildSaveGameInstance / ApplyLoadedSaveGame and the four entry points over
//                 them -- SaveWorld and LoadWorld go through the engine's named SaveGame slots,
//                 SaveWorldToFile and LoadWorldFromFile through an explicit path so a world can
//                 be handed around as a file.
//
//   single chunk  GetChunkCachePath / SaveChunkToCache / LoadChunkFromCache, the zlib-compressed
//                 per-chunk cache the streamer writes to before evicting a dirty chunk and reads
//                 back when that coordinate returns. Grouped here because it is the same job --
//                 serialising state to disk -- even though its caller is the streamer.
//
// Deliberately NOT moved: ApplyWorldPreset and SetAuthoredChunkCoordinates. Both sit next to the
// save/load code and both write manager configuration, but neither touches disk, so pulling them
// in would have made "persistence" mean "anything near the save code".

#include "Core/GRTManager.h"
#include "Core/GRTManagerInternal.h"
#include "Core/GRTSaveGame.h"
#include "Core/GRTSubsystem.h"
#include "Core/GRTEditController.h"
#include "Core/GRTWorldPreset.h"
#include "Settings/GRTSettings.h"
#include "World/GRTChunk.h"
#include "Building/GRTBuildingActor.h"
#include "Building/GRTPlacedModelActor.h"
#include "Core/GRTAssetLibrarySubsystem.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "HAL/FileManager.h"
#include "NavigationSystem.h"
#include "IO/GRTGeoTIFFHeightSource.h"

UGRTSaveGame* AGRTManager::BuildSaveGameInstance()
{
	UGRTSaveGame* SaveInst = Cast<UGRTSaveGame>(UGameplayStatics::CreateSaveGameObject(UGRTSaveGame::StaticClass()));
	if (!SaveInst) return nullptr;

	SaveInst->Timestamp = FDateTime::Now();
	SaveInst->ManagerLocation = GetActorLocation();
	SaveInst->SavedGlobalChunkSize = GlobalChunkSize;
	SaveInst->LandscapeMigration = LandscapeMigrationState;
	SaveInst->WorldOriginKind = WorldOriginKind;
	SaveInst->GeoTIFFImport = GeoTIFFImportState;
	SaveInst->ProceduralWorldSettings = ProceduralWorldSettings;
	SaveInst->BiomeOverlays = BiomeOverlays;
	SaveInst->PopulationSpawnRules = PopulationSpawnRules;
	SaveInst->AISpawnZones = AISpawnZones;
	SaveInst->BuildPermissionZones = BuildPermissionZones;
	SaveInst->ProceduralChunks = ProceduralChunkStates;
	SaveInst->PersistentPopulationEntries = PersistentPopulationEntries;
	SaveInst->AuthoredChunkCoords.Reset(ImportedChunkCoords.Num());
	for (const FIntPoint& Coord : ImportedChunkCoords)
	{
		SaveInst->AuthoredChunkCoords.Add(Coord);
	}

	if (MasterMaterial)
	{
		const FString MaterialPath = MasterMaterial->GetPathName();
		if (!MaterialPath.StartsWith(TEXT("/Engine/Transient")))
		{
			SaveInst->MasterMaterialPath = MaterialPath;
		}
	}

	SaveInst->Buildings.Reset();
	for (const TWeakObjectPtr<AGRTBuildingActor>& Entry : RegisteredBuildings)
	{
		if (AGRTBuildingActor* Building = Entry.Get())
		{
			SaveInst->Buildings.Add(Building->ToData());
		}
	}

	// Imported models: the catalog (source paths only -- runtime geometry belongs to no package and
	// cannot be serialised) and every placed instance, found by iteration since placed models have
	// no registry of their own the way buildings do.
	SaveInst->ModelLibrary.Reset();
	SaveInst->PlacedModels.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UGRTAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UGRTAssetLibrarySubsystem>())
		{
			SaveInst->ModelLibrary = AssetLib->ExportLibrary();
		}

		for (TActorIterator<AGRTPlacedModelActor> It(World); It; ++It)
		{
			if (AGRTPlacedModelActor* Model = *It)
			{
				if (!Model->IsActorBeingDestroyed())
				{
					SaveInst->PlacedModels.Add(Model->ToData());
				}
			}
		}
	}

	for (auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			SaveInst->Chunks.Add(Pair.Key, Pair.Value->GetSerializedData());
		}
	}

	// Edited chunks that streamed out are only on disk in this session's eviction cache. Without
	// this they would be missing from the save, and their edits lost once the session ends.
	for (const FIntPoint& Coord : CachedChunkCoords)
	{
		if (SaveInst->Chunks.Contains(Coord))
		{
			continue; // the live chunk is newer than its cached copy
		}
		FGRTChunkData CachedData;
		if (LoadChunkFromCache(Coord, CachedData))
		{
			SaveInst->Chunks.Add(Coord, MoveTemp(CachedData));
		}
	}

	return SaveInst;
}

void AGRTManager::SaveWorld(FString SlotName)
{
	if (SlotName.IsEmpty()) SlotName = TEXT("GRTSave");

	UGRTSaveGame* SaveInst = BuildSaveGameInstance();
	if (!SaveInst) return;

	if (UGameplayStatics::SaveGameToSlot(SaveInst, SlotName, 0))
	{
		UE_LOG(LogTemp, Log, TEXT("GRT: World saved to slot '%s' with %d chunks."), *SlotName, SaveInst->Chunks.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("GRT: Failed to save to slot '%s'"), *SlotName);
	}
}

bool AGRTManager::SaveWorldToFile(const FString& FilePath)
{
	UGRTSaveGame* SaveInst = BuildSaveGameInstance();
	if (!SaveInst) return false;

	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(SaveInst, Bytes))
	{
		UE_LOG(LogTemp, Error, TEXT("GRT: Failed to serialize world for export to '%s'"), *FilePath);
		return false;
	}

	if (!FFileHelper::SaveArrayToFile(Bytes, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("GRT: Failed to write world file '%s'"), *FilePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("GRT: World exported to '%s' with %d chunks."), *FilePath, SaveInst->Chunks.Num());
	return true;
}

void AGRTManager::LoadWorld(FString SlotName)
{
	if (SlotName.IsEmpty()) SlotName = TEXT("GRTSave");

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("GRT: No save game found at slot '%s'"), *SlotName);
		return;
	}

	UGRTSaveGame* LoadInst = Cast<UGRTSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!LoadInst) return;

	ApplyLoadedSaveGame(LoadInst, FString::Printf(TEXT("slot '%s'"), *SlotName));
}

bool AGRTManager::LoadWorldFromFile(const FString& FilePath)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("GRT: World file not found or unreadable: '%s'"), *FilePath);
		return false;
	}

	UGRTSaveGame* LoadInst = Cast<UGRTSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!LoadInst)
	{
		UE_LOG(LogTemp, Error, TEXT("GRT: Failed to deserialize world file '%s'"), *FilePath);
		return false;
	}

	ApplyLoadedSaveGame(LoadInst, FString::Printf(TEXT("file '%s'"), *FilePath));
	return true;
}

void AGRTManager::ApplyLoadedSaveGame(UGRTSaveGame* LoadInst, const FString& SourceLabel)
{
	// Clear streaming state — full world load resets everything
	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkSet.Empty();
	ImportedChunkCoords.Empty();
	LastStreamingHash = 0;
	ClearUndoRedoState();
	// The eviction cache holds edits to the world being replaced; streaming must not reload them.
	ResetChunkCacheSession();
	// Building/model history refers to actors this load is about to destroy. Left in place, undoing
	// a pre-load delete would resurrect that building into the loaded world.
	if (UWorld* HistoryWorld = GetWorld())
	{
		for (TActorIterator<AGRTEditController> It(HistoryWorld); It; ++It)
		{
			It->ResetActorHistory();
		}
	}
	DestroyTrackedPopulationActors();
	PersistentPopulationEntries.Reset();
	ProceduralChunkStates.Reset();
	BiomeOverlays.Reset();
	PopulationSpawnRules.Reset();
	AISpawnZones.Reset();
	BuildPermissionZones.Reset();
	RebuildWorldStateIndices();

	UE_LOG(LogTemp, Log, TEXT("GRT: Loading world from %s..."), *SourceLabel);

	SetActorLocation(LoadInst->ManagerLocation);
	if (LoadInst->SavedGlobalChunkSize > KINDA_SMALL_NUMBER)
	{
		GlobalChunkSize = LoadInst->SavedGlobalChunkSize;
	}

	LandscapeMigrationState = LoadInst->LandscapeMigration;
	WorldOriginKind = LoadInst->WorldOriginKind;
	GeoTIFFImportState = LoadInst->GeoTIFFImport;
	if (WorldOriginKind == EGRTChunkOrigin::Procedural && LandscapeMigrationState.bWasImportedFromLandscape)
	{
		// Save predates WorldOriginKind (added alongside GeoTIFF import) -- back-fill it from the
		// older, Landscape-only flag so LoadOrSpawnChunk's origin check still behaves correctly.
		WorldOriginKind = EGRTChunkOrigin::ImportedLandscape;
	}
	ActiveGeoTIFFHeightSource.Reset();
	ProceduralWorldSettings = LoadInst->ProceduralWorldSettings;
	BiomeOverlays = LoadInst->BiomeOverlays;
	PopulationSpawnRules = LoadInst->PopulationSpawnRules;
	AISpawnZones = LoadInst->AISpawnZones;
	BuildPermissionZones = LoadInst->BuildPermissionZones;
	ProceduralChunkStates = LoadInst->ProceduralChunks;
	PersistentPopulationEntries = LoadInst->PersistentPopulationEntries;
	for (const FIntPoint& Coord : LoadInst->AuthoredChunkCoords)
	{
		ImportedChunkCoords.Add(Coord);
	}
	RebuildWorldStateIndices();
	if (!LoadInst->MasterMaterialPath.IsEmpty())
	{
		if (UMaterialInterface* LoadedMaterial = LoadObject<UMaterialInterface>(nullptr, *LoadInst->MasterMaterialPath))
		{
			MasterMaterial = LoadedMaterial;
		}
	}

	TSet<FIntPoint> SavedCoords;
	for (const auto& Pair : LoadInst->Chunks)
	{
		SavedCoords.Add(Pair.Key);
	}

	TArray<FIntPoint> ExistingCoords;
	ActiveChunkMap.GetKeys(ExistingCoords);
	for (const FIntPoint& Coord : ExistingCoords)
	{
		if (!SavedCoords.Contains(Coord))
		{
			if (AGRTChunk* Chunk = GetChunkAtCoord(Coord))
			{
				ActiveChunkMap.Remove(Coord);
				Chunk->Destroy();
			}
		}
	}

	for (const auto& Pair : LoadInst->Chunks)
	{
		FIntPoint Coord = Pair.Key;
		const FGRTChunkData& Data = Pair.Value;
		if (LoadInst->AuthoredChunkCoords.Num() == 0 && LandscapeMigrationState.bWasImportedFromLandscape)
		{
			ImportedChunkCoords.Add(Coord);
		}

		AGRTChunk* Chunk = nullptr;

		if (ActiveChunkMap.Contains(Coord))
		{
			Chunk = ActiveChunkMap[Coord];
		}
		else
		{
			Chunk = SpawnConfiguredChunk(Coord, Data.Resolution);
		}

		if (Chunk)
		{
			Chunk->LoadFromData(Data);
			// Loaded state differs from what the chunk would regenerate, so it must be cached if it
			// streams out -- otherwise flying away and back silently reverts it.
			DirtyChunkSet.Add(Coord);
		}
	}

	// Destroy existing buildings
	TArray<AGRTBuildingActor*> BuildingsToDestroy;
	for (const TWeakObjectPtr<AGRTBuildingActor>& Entry : RegisteredBuildings)
	{
		if (AGRTBuildingActor* Building = Entry.Get())
		{
			BuildingsToDestroy.Add(Building);
		}
	}
	RegisteredBuildings.Reset();
	for (AGRTBuildingActor* Building : BuildingsToDestroy)
	{
		if (Building)
		{
			Building->Destroy();
		}
	}

	// Destroy placed models and rebuild the catalog before respawning, mirroring how buildings are
	// torn down above. Restoring the library first matters: each placement resolves its geometry
	// through the catalog, so the entries have to be back under their saved ids before any of them
	// spawn, or every model would come up empty.
	if (UWorld* ModelWorld = GetWorld())
	{
		TArray<AGRTPlacedModelActor*> ModelsToDestroy;
		for (TActorIterator<AGRTPlacedModelActor> It(ModelWorld); It; ++It)
		{
			ModelsToDestroy.Add(*It);
		}
		for (AGRTPlacedModelActor* Model : ModelsToDestroy)
		{
			if (Model && !Model->IsActorBeingDestroyed())
			{
				Model->Destroy();
			}
		}

		if (UGRTAssetLibrarySubsystem* AssetLib = ModelWorld->GetSubsystem<UGRTAssetLibrarySubsystem>())
		{
			const int32 Restored = AssetLib->RestoreLibrary(LoadInst->ModelLibrary);
			if (Restored < LoadInst->ModelLibrary.Num())
			{
				UE_LOG(LogTemp, Warning, TEXT("GRT: restored %d of %d model catalog entries; the rest could not be re-imported from their saved paths."),
					Restored, LoadInst->ModelLibrary.Num());
			}
		}

		if (HasAuthority())
		{
			for (const FGRTPlacedModelData& MData : LoadInst->PlacedModels)
			{
				FActorSpawnParameters ModelSpawnParams;
				ModelSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AGRTPlacedModelActor* NewModel = ModelWorld->SpawnActor<AGRTPlacedModelActor>(
					AGRTPlacedModelActor::StaticClass(), MData.WorldTransform, ModelSpawnParams);
				if (NewModel)
				{
					NewModel->ApplyData(MData);
				}
			}
		}
	}

	// Spawn saved buildings
	UWorld* World = GetWorld();
	if (World && HasAuthority())
	{
		for (const FGRTBuildingData& BData : LoadInst->Buildings)
		{
			AGRTBuildingActor::SpawnFromData(World, BData);
		}
	}

	if (World && HasAuthority() && WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF)
	{
		// Chunks already in LoadInst->Chunks are restored from their own per-chunk data above and
		// don't need this; it's purely so chunks that stream in for the first time *after* this
		// reload (never visited before the restart) still resolve correct DEM data instead of
		// silently falling back to plain procedural noise.
		if (!GeoTIFFImportState.SourceFilePath.IsEmpty())
		{
			FGRTGeoTIFFImportOptions ReloadOptions = GeoTIFFImportOptions;
			ReloadOptions.bClearExistingChunks = false;
			ReloadOptions.bEstablishGeoreferenceOrigin = false;
			ImportGeoTIFFInternal(GeoTIFFImportState.SourceFilePath, ReloadOptions);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GRT: Loaded world was imported from GeoTIFF but has no source path; chunks not yet visited will fall back to procedural."));
		}
	}

	RebuildChunkMap();
	ApplyMaterialToActiveChunks();
	OnRep_FrameworkConfig();
	RefreshPopulationForLoadedChunks();

	// Push full sync to all connected player controllers so clients see the loaded terrain and state
	if (World && HasAuthority())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (AGRTEditController* EditController = Cast<AGRTEditController>(It->Get()))
			{
				SendFullSyncToController(EditController);
			}
		}
	}
}

FString AGRTManager::GetChunkCacheRoot() const
{
	FString Dir = TEXT("GRT/ChunkCache");
	if (const UGRTSettings* S = GetDefault<UGRTSettings>())
	{
		Dir = S->ChunkSaveDir;
	}
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Dir);
}

FString AGRTManager::GetChunkCacheSessionDir() const
{
	return FPaths::Combine(GetChunkCacheRoot(), ChunkCacheSessionId.ToString(EGuidFormats::Digits));
}

FString AGRTManager::GetChunkCachePath(FIntPoint Coord) const
{
	// Scoped to one world session. The cache used to be one flat folder shared by every session and
	// world, and was never cleared, so a chunk sculpted and evicted in one session was loaded back
	// over a different world -- a new procedural seed, an imported DEM, a loaded save -- whenever
	// streaming reached the same coordinate.
	return FPaths::Combine(GetChunkCacheSessionDir(), FString::Printf(TEXT("%d_%d.chunk"), Coord.X, Coord.Y));
}

void AGRTManager::ResetChunkCacheSession()
{
	IFileManager::Get().DeleteDirectory(*GetChunkCacheSessionDir(), /*RequireExists=*/false, /*Tree=*/true);
	CachedChunkCoords.Reset();
	ChunkCacheSessionId = FGuid::NewGuid();
}

void AGRTManager::PurgeStaleChunkCacheSessions() const
{
	IFileManager& FileManager = IFileManager::Get();
	const FString Root = GetChunkCacheRoot();
	if (!FileManager.DirectoryExists(*Root))
	{
		return;
	}

	// Loose files at the root are from before the cache was per-session. Nothing reads them any
	// more, and they are exactly the stale data that leaked across worlds.
	TArray<FString> LegacyFiles;
	FileManager.FindFiles(LegacyFiles, *FPaths::Combine(Root, TEXT("*.chunk")), /*Files=*/true, /*Directories=*/false);
	for (const FString& File : LegacyFiles)
	{
		FileManager.Delete(*FPaths::Combine(Root, File), false, true, true);
	}

	// A session folder normally goes away in EndPlay; one left behind means a crash or a killed
	// process. Only GUID-named folders are touched, and only once they are a day old, so a second
	// game instance running right now (multiplayer PIE, a local server) keeps its own.
	TArray<FString> SessionDirs;
	FileManager.FindFiles(SessionDirs, *FPaths::Combine(Root, TEXT("*")), /*Files=*/false, /*Directories=*/true);
	const FDateTime Cutoff = FDateTime::UtcNow() - FTimespan::FromHours(24.0);
	for (const FString& DirName : SessionDirs)
	{
		FGuid ParsedId;
		if (!FGuid::Parse(DirName, ParsedId))
		{
			continue;
		}
		const FString FullDir = FPaths::Combine(Root, DirName);
		const FFileStatData Stat = FileManager.GetStatData(*FullDir);
		if (Stat.bIsValid && Stat.ModificationTime < Cutoff)
		{
			FileManager.DeleteDirectory(*FullDir, false, true);
		}
	}
}

void AGRTManager::SaveChunkToCache(FIntPoint Coord, const FGRTChunkData& Data)
{
	TArray<uint8> RawBytes;
	FMemoryWriter Ar(RawBytes, true);
	FGRTChunkData MutableData = Data;
	Ar << MutableData.Coordinate;
	Ar << MutableData.Resolution;
	Ar << MutableData.ZScale;
	Ar << MutableData.HeightData;
	Ar << MutableData.BaseData;
	Ar << MutableData.SculptData;
	Ar << MutableData.DetailData;
	Ar << MutableData.bTransferredFoliageFollowsTerrain;
	Ar << MutableData.FoliageStaticMeshPaths;
	Ar << MutableData.FoliageMaterialCounts;
	Ar << MutableData.FoliageOverrideMaterialPaths;
	Ar << MutableData.FoliageDefinitionIndices;
	Ar << MutableData.FoliageInstanceLocalTransforms;
	Ar << MutableData.FoliageInstanceTerrainOffsets;
	Ar << MutableData.ActorFoliageClassPaths;
	Ar << MutableData.ActorFoliageAttachFlags;
	Ar << MutableData.ActorFoliageDefinitionIndices;
	Ar << MutableData.ActorFoliageInstanceLocalTransforms;
	Ar << MutableData.ActorFoliageInstanceTerrainOffsets;

	// Compress with Zlib
	int32 UncompressedSize = RawBytes.Num();
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, UncompressedSize));
	int32 CompressedSize = Compressed.Num();
	if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, RawBytes.GetData(), UncompressedSize))
	{
		Compressed.SetNum(CompressedSize);

		// File format: [UncompressedSize:int32][CompressedData]
		TArray<uint8> FileData;
		FileData.SetNumUninitialized(sizeof(int32) + CompressedSize);
		FMemory::Memcpy(FileData.GetData(), &UncompressedSize, sizeof(int32));
		FMemory::Memcpy(FileData.GetData() + sizeof(int32), Compressed.GetData(), CompressedSize);

		FString Path = GetChunkCachePath(Coord);
		FString FileDir = FPaths::GetPath(Path);
		IFileManager::Get().MakeDirectory(*FileDir, true);
		if (!FFileHelper::SaveArrayToFile(FileData, *Path))
		{
			UE_LOG(LogTemp, Warning, TEXT("Streaming: Failed to write cache file for chunk [%d,%d] at %s"), Coord.X, Coord.Y, *Path);
		}
		else
		{
			CachedChunkCoords.Add(Coord);
			UE_LOG(LogTemp, Verbose, TEXT("Streaming: Cached chunk [%d,%d] (%d bytes compressed)"), Coord.X, Coord.Y, CompressedSize);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Streaming: Failed to compress chunk [%d,%d]"), Coord.X, Coord.Y);
	}
}

bool AGRTManager::LoadChunkFromCache(FIntPoint Coord, FGRTChunkData& OutData)
{
	FString Path = GetChunkCachePath(Coord);
	if (!IFileManager::Get().FileExists(*Path))
	{
		return false;
	}
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		return false;
	}
	if (FileData.Num() <= (int32)sizeof(int32))
	{
		return false;
	}

	int32 UncompressedSize = 0;
	FMemory::Memcpy(&UncompressedSize, FileData.GetData(), sizeof(int32));

	// Sanity check: reject obviously invalid sizes
	constexpr int32 MaxReasonableSize = 64 * 1024 * 1024; // 64 MB
	if (UncompressedSize <= 0 || UncompressedSize > MaxReasonableSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("Streaming: Invalid uncompressed size %d for chunk [%d,%d]"),
			UncompressedSize, Coord.X, Coord.Y);
		return false;
	}

	TArray<uint8> Decompressed;
	Decompressed.SetNumUninitialized(UncompressedSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Decompressed.GetData(), UncompressedSize,
		FileData.GetData() + sizeof(int32), FileData.Num() - sizeof(int32)))
	{
		UE_LOG(LogTemp, Warning, TEXT("Streaming: Failed to decompress chunk [%d,%d]"), Coord.X, Coord.Y);
		return false;
	}

	FMemoryReader Ar(Decompressed, true);
	Ar << OutData.Coordinate;
	Ar << OutData.Resolution;
	Ar << OutData.ZScale;
	Ar << OutData.HeightData;
	Ar << OutData.BaseData;
	Ar << OutData.SculptData;
	Ar << OutData.DetailData;
	if (!Ar.AtEnd())
	{
		Ar << OutData.bTransferredFoliageFollowsTerrain;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.FoliageStaticMeshPaths;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.FoliageMaterialCounts;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.FoliageOverrideMaterialPaths;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.FoliageDefinitionIndices;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.FoliageInstanceLocalTransforms;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.FoliageInstanceTerrainOffsets;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.ActorFoliageClassPaths;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.ActorFoliageAttachFlags;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.ActorFoliageDefinitionIndices;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.ActorFoliageInstanceLocalTransforms;
	}
	if (!Ar.AtEnd())
	{
		Ar << OutData.ActorFoliageInstanceTerrainOffsets;
	}

	return true;
}
