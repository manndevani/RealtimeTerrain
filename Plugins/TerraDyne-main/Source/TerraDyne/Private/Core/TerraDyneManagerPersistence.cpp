// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// ATerraDyneManager -- persistence.
//
// Third translation unit split out of TerraDyneManager.cpp. Same members, same class, same API;
// only the file boundary is new. See TerraDyneManagerPopulation.cpp for the first.
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

#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneManagerInternal.h"
#include "Core/TerraDyneSaveGame.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneWorldPreset.h"
#include "Settings/TerraDyneSettings.h"
#include "World/TerraDyneChunk.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "HAL/FileManager.h"
#include "NavigationSystem.h"

UTerraDyneSaveGame* ATerraDyneManager::BuildSaveGameInstance()
{
	UTerraDyneSaveGame* SaveInst = Cast<UTerraDyneSaveGame>(UGameplayStatics::CreateSaveGameObject(UTerraDyneSaveGame::StaticClass()));
	if (!SaveInst) return nullptr;

	SaveInst->Timestamp = FDateTime::Now();
	SaveInst->ManagerLocation = GetActorLocation();
	SaveInst->SavedGlobalChunkSize = GlobalChunkSize;
	SaveInst->LandscapeMigration = LandscapeMigrationState;
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
	for (const TWeakObjectPtr<ATerraDyneBuildingActor>& Entry : RegisteredBuildings)
	{
		if (ATerraDyneBuildingActor* Building = Entry.Get())
		{
			SaveInst->Buildings.Add(Building->ToData());
		}
	}

	for (auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			SaveInst->Chunks.Add(Pair.Key, Pair.Value->GetSerializedData());
		}
	}

	return SaveInst;
}

void ATerraDyneManager::SaveWorld(FString SlotName)
{
	if (SlotName.IsEmpty()) SlotName = TEXT("TerraDyneSave");

	UTerraDyneSaveGame* SaveInst = BuildSaveGameInstance();
	if (!SaveInst) return;

	if (UGameplayStatics::SaveGameToSlot(SaveInst, SlotName, 0))
	{
		UE_LOG(LogTemp, Log, TEXT("TerraDyne: World saved to slot '%s' with %d chunks."), *SlotName, SaveInst->Chunks.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TerraDyne: Failed to save to slot '%s'"), *SlotName);
	}
}

bool ATerraDyneManager::SaveWorldToFile(const FString& FilePath)
{
	UTerraDyneSaveGame* SaveInst = BuildSaveGameInstance();
	if (!SaveInst) return false;

	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(SaveInst, Bytes))
	{
		UE_LOG(LogTemp, Error, TEXT("TerraDyne: Failed to serialize world for export to '%s'"), *FilePath);
		return false;
	}

	if (!FFileHelper::SaveArrayToFile(Bytes, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("TerraDyne: Failed to write world file '%s'"), *FilePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("TerraDyne: World exported to '%s' with %d chunks."), *FilePath, SaveInst->Chunks.Num());
	return true;
}

void ATerraDyneManager::LoadWorld(FString SlotName)
{
	if (SlotName.IsEmpty()) SlotName = TEXT("TerraDyneSave");

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyne: No save game found at slot '%s'"), *SlotName);
		return;
	}

	UTerraDyneSaveGame* LoadInst = Cast<UTerraDyneSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!LoadInst) return;

	ApplyLoadedSaveGame(LoadInst, FString::Printf(TEXT("slot '%s'"), *SlotName));
}

bool ATerraDyneManager::LoadWorldFromFile(const FString& FilePath)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyne: World file not found or unreadable: '%s'"), *FilePath);
		return false;
	}

	UTerraDyneSaveGame* LoadInst = Cast<UTerraDyneSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!LoadInst)
	{
		UE_LOG(LogTemp, Error, TEXT("TerraDyne: Failed to deserialize world file '%s'"), *FilePath);
		return false;
	}

	ApplyLoadedSaveGame(LoadInst, FString::Printf(TEXT("file '%s'"), *FilePath));
	return true;
}

void ATerraDyneManager::ApplyLoadedSaveGame(UTerraDyneSaveGame* LoadInst, const FString& SourceLabel)
{
	// Clear streaming state — full world load resets everything
	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkSet.Empty();
	ImportedChunkCoords.Empty();
	LastStreamingHash = 0;
	ClearUndoRedoState();
	DestroyTrackedPopulationActors();
	PersistentPopulationEntries.Reset();
	ProceduralChunkStates.Reset();
	BiomeOverlays.Reset();
	PopulationSpawnRules.Reset();
	AISpawnZones.Reset();
	BuildPermissionZones.Reset();
	RebuildWorldStateIndices();

	UE_LOG(LogTemp, Log, TEXT("TerraDyne: Loading world from %s..."), *SourceLabel);

	SetActorLocation(LoadInst->ManagerLocation);
	if (LoadInst->SavedGlobalChunkSize > KINDA_SMALL_NUMBER)
	{
		GlobalChunkSize = LoadInst->SavedGlobalChunkSize;
	}

	LandscapeMigrationState = LoadInst->LandscapeMigration;
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
			if (ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord))
			{
				ActiveChunkMap.Remove(Coord);
				Chunk->Destroy();
			}
		}
	}

	for (const auto& Pair : LoadInst->Chunks)
	{
		FIntPoint Coord = Pair.Key;
		const FTerraDyneChunkData& Data = Pair.Value;
		if (LoadInst->AuthoredChunkCoords.Num() == 0 && LandscapeMigrationState.bWasImportedFromLandscape)
		{
			ImportedChunkCoords.Add(Coord);
		}

		ATerraDyneChunk* Chunk = nullptr;

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
		}
	}

	// Destroy existing buildings
	TArray<ATerraDyneBuildingActor*> BuildingsToDestroy;
	for (const TWeakObjectPtr<ATerraDyneBuildingActor>& Entry : RegisteredBuildings)
	{
		if (ATerraDyneBuildingActor* Building = Entry.Get())
		{
			BuildingsToDestroy.Add(Building);
		}
	}
	RegisteredBuildings.Reset();
	for (ATerraDyneBuildingActor* Building : BuildingsToDestroy)
	{
		if (Building)
		{
			Building->Destroy();
		}
	}

	// Spawn saved buildings
	UWorld* World = GetWorld();
	if (World && HasAuthority())
	{
		for (const FTerraDyneBuildingData& BData : LoadInst->Buildings)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATerraDyneBuildingActor* NewBuilding = World->SpawnActor<ATerraDyneBuildingActor>(
				ATerraDyneBuildingActor::StaticClass(),
				FTransform(BData.Rotation, BData.Origin),
				SpawnParams);
			if (NewBuilding)
			{
				NewBuilding->ApplyData(BData, true);
			}
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
			if (ATerraDyneEditController* EditController = Cast<ATerraDyneEditController>(It->Get()))
			{
				SendFullSyncToController(EditController);
			}
		}
	}
}

FString ATerraDyneManager::GetChunkCachePath(FIntPoint Coord) const
{
	FString Dir = TEXT("TerraDyne/ChunkCache");
	if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		Dir = S->ChunkSaveDir;
	}
	FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Dir);
	return FPaths::Combine(SaveDir, FString::Printf(TEXT("%d_%d.chunk"), Coord.X, Coord.Y));
}

void ATerraDyneManager::SaveChunkToCache(FIntPoint Coord, const FTerraDyneChunkData& Data)
{
	TArray<uint8> RawBytes;
	FMemoryWriter Ar(RawBytes, true);
	FTerraDyneChunkData MutableData = Data;
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
			UE_LOG(LogTemp, Verbose, TEXT("Streaming: Cached chunk [%d,%d] (%d bytes compressed)"), Coord.X, Coord.Y, CompressedSize);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Streaming: Failed to compress chunk [%d,%d]"), Coord.X, Coord.Y);
	}
}

bool ATerraDyneManager::LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData)
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
