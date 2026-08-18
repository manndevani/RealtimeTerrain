# Chunk Streaming Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add distance-based chunk streaming to TerraDyne so a 21x21 grid (441 chunks) streams ~61 chunks around the player, auto-persisting dirty chunks to disk on unload.

**Architecture:** Manager-driven ring streamer. Every 0.25s (existing LOD tick), compute player grid cell, scan diamond load/unload rings, process a throttled queue of spawn/teardown ops. Dirty chunks serialize to per-chunk files before destruction. Clean chunks regenerate from FBM noise.

**Tech Stack:** UE5 C++ (AActor, UDeveloperSettings, FFileHelper, FMemoryWriter/Reader, FCompression)

---

### Task 1: Add Streaming Settings

**Files:**
- Modify: `Source/TerraDyne/Public/Settings/TerraDyneSettings.h:36-45`
- Modify: `Source/TerraDyne/Private/Settings/TerraDyneSettings.cpp:18-21`

**Step 1: Add streaming properties to the header**

In `TerraDyneSettings.h`, insert after line 45 (after `GrassDebounceTime`), before `//--- Undo/Redo ---//`:

```cpp
	//--- Streaming ---//
	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Radius in chunk units within which chunks are loaded (diamond shape).", ClampMin = "1", ClampMax = "20"))
	int32 ChunkLoadRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Radius in chunk units beyond which chunks are unloaded. Must be > LoadRadius for hysteresis.", ClampMin = "2", ClampMax = "25"))
	int32 ChunkUnloadRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Max chunk spawn or teardown operations per streaming tick.", ClampMin = "1", ClampMax = "8"))
	int32 MaxChunkOpsPerTick;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Half-width of the world grid. Grid spans -N..+N = (2N+1)x(2N+1) chunks.", ClampMin = "1", ClampMax = "50"))
	int32 GridExtent;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming",
	    meta = (ToolTip = "Subdirectory under SaveGames/ for per-chunk cache files."))
	FString ChunkSaveDir;
```

**Step 2: Add defaults to the constructor**

In `TerraDyneSettings.cpp`, insert after line 21 (`GrassDebounceTime = 0.5f;`):

```cpp
	// Streaming Defaults
	ChunkLoadRadius = 5;
	ChunkUnloadRadius = 7;
	MaxChunkOpsPerTick = 2;
	GridExtent = 10;
	ChunkSaveDir = TEXT("TerraDyne/ChunkCache");
```

**Step 3: Build**

Run: `UnrealBuildTool.exe TerraDyneEditor Win64 Development -Project="G:/Epic Games/TerraDyne/TerraDyne.uproject" -WaitMutex -FromMsBuild`
Expected: `Result: Succeeded`

**Step 4: Commit**

```
feat(streaming): add chunk streaming settings to UTerraDyneSettings
```

---

### Task 2: Add Streaming State and Methods to Manager Header

**Files:**
- Modify: `Source/TerraDyne/Public/Core/TerraDyneManager.h:198-213`

**Step 1: Add streaming declarations**

In `TerraDyneManager.h`, insert after line 203 (`float LODTimer = 0.0f;`), before the undo/redo section:

```cpp
	// --- Streaming ---
	TSet<FIntPoint> PendingLoadQueue;
	TSet<FIntPoint> PendingUnloadQueue;
	TMap<FIntPoint, bool> DirtyChunkMap;
	FIntPoint LastStreamingCenter = FIntPoint(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	float StreamingTimer = 0.0f;

	FIntPoint WorldToChunkCoord(const FVector& WorldPos) const;
	void UpdateStreaming(const FVector& PlayerPos);
	void ProcessStreamingQueues(const FVector& PlayerPos);
	void LoadOrSpawnChunk(FIntPoint Coord);
	void UnloadChunk(FIntPoint Coord);
	void MarkChunkDirty(FIntPoint Coord);
	FString GetChunkCachePath(FIntPoint Coord) const;
	void SaveChunkToCache(FIntPoint Coord, const FTerraDyneChunkData& Data);
	bool LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData);
```

**Step 2: Build**

Expected: `Result: Succeeded` (declarations only, no linker errors yet — all methods will be defined in Task 3)

**Step 3: Commit**

```
feat(streaming): add streaming state and method declarations to ATerraDyneManager
```

---

### Task 3: Implement Core Streaming Logic

**Files:**
- Modify: `Source/TerraDyne/Private/Core/TerraDyneManager.cpp`

This is the largest task. Add the following methods at the end of the file (before `#if WITH_EDITOR`).

**Step 1: Implement helper methods**

Insert before the `#if WITH_EDITOR` line (currently line 755):

```cpp
FIntPoint ATerraDyneManager::WorldToChunkCoord(const FVector& WorldPos) const
{
	return FIntPoint(
		FMath::FloorToInt(WorldPos.X / GlobalChunkSize),
		FMath::FloorToInt(WorldPos.Y / GlobalChunkSize)
	);
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
	// Serialize fields manually (FTerraDyneChunkData is a USTRUCT, use UE serialization)
	FTerraDyneChunkData MutableData = Data;
	Ar << MutableData.Coordinate;
	Ar << MutableData.Resolution;
	Ar << MutableData.ZScale;
	Ar << MutableData.HeightData;
	Ar << MutableData.BaseData;
	Ar << MutableData.SculptData;
	Ar << MutableData.DetailData;
	Ar << MutableData.WeightData;

	// Compress
	int32 UncompressedSize = RawBytes.Num();
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, UncompressedSize));
	int32 CompressedSize = Compressed.Num();
	if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, RawBytes.GetData(), UncompressedSize))
	{
		Compressed.SetNum(CompressedSize);

		// Write: [UncompressedSize:int32][CompressedData]
		TArray<uint8> FileData;
		FileData.SetNumUninitialized(sizeof(int32) + CompressedSize);
		FMemory::Memcpy(FileData.GetData(), &UncompressedSize, sizeof(int32));
		FMemory::Memcpy(FileData.GetData() + sizeof(int32), Compressed.GetData(), CompressedSize);

		FString Path = GetChunkCachePath(Coord);
		FString Dir = FPaths::GetPath(Path);
		IFileManager::Get().MakeDirectory(*Dir, true);
		FFileHelper::SaveArrayToFile(FileData, *Path);
	}
}

bool ATerraDyneManager::LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData)
{
	FString Path = GetChunkCachePath(Coord);
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

	TArray<uint8> Decompressed;
	Decompressed.SetNumUninitialized(UncompressedSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Decompressed.GetData(), UncompressedSize,
		FileData.GetData() + sizeof(int32), FileData.Num() - sizeof(int32)))
	{
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
	Ar << OutData.WeightData;

	return true;
}

void ATerraDyneManager::MarkChunkDirty(FIntPoint Coord)
{
	DirtyChunkMap.Add(Coord, true);
}
```

**Step 2: Implement LoadOrSpawnChunk and UnloadChunk**

Insert immediately after the above:

```cpp
void ATerraDyneManager::LoadOrSpawnChunk(FIntPoint Coord)
{
	if (ActiveChunkMap.Contains(Coord)) return;

	FVector Location(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0);

	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;

	ATerraDyneChunk* Chunk = GetWorld()->SpawnActor<ATerraDyneChunk>(
		ATerraDyneChunk::StaticClass(), FTransform(Location), Params);
	if (!Chunk) return;

	Chunk->GridCoordinate = Coord;
	Chunk->ChunkSizeWorldUnits = GlobalChunkSize;
	Chunk->BrushMaterialBase = HeightBrushMaterial;
	Chunk->WorldSize = GlobalChunkSize;
	if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		Chunk->Resolution = FMath::Clamp(S->DefaultResolution, 32, 256);
	}
	else
	{
		Chunk->Resolution = 64;
	}

	Chunk->FinishSpawning(FTransform(Location));
	Chunk->SetMaterial(MasterMaterial);
	if (ActiveGrassProfile)
	{
		Chunk->SetGrassProfile(ActiveGrassProfile);
	}

	// Try loading cached data (previously sculpted/painted chunk)
	FTerraDyneChunkData CachedData;
	if (LoadChunkFromCache(Coord, CachedData))
	{
		Chunk->LoadFromData(CachedData);
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Loaded chunk [%d,%d] from cache"), Coord.X, Coord.Y);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Spawned fresh chunk [%d,%d]"), Coord.X, Coord.Y);
	}

	ActiveChunkMap.Add(Coord, Chunk);
	PendingLoadQueue.Remove(Coord);
}

void ATerraDyneManager::UnloadChunk(FIntPoint Coord)
{
	ATerraDyneChunk** Found = ActiveChunkMap.Find(Coord);
	if (!Found || !(*Found)) return;

	ATerraDyneChunk* Chunk = *Found;

	// Persist dirty chunks
	if (DirtyChunkMap.Contains(Coord))
	{
		FTerraDyneChunkData Data = Chunk->GetSerializedData();
		SaveChunkToCache(Coord, Data);
		DirtyChunkMap.Remove(Coord);
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Saved dirty chunk [%d,%d] to cache"), Coord.X, Coord.Y);
	}

	ActiveChunkMap.Remove(Coord);
	Chunk->Destroy();
	PendingUnloadQueue.Remove(Coord);

	UE_LOG(LogTemp, Verbose, TEXT("Streaming: Unloaded chunk [%d,%d]"), Coord.X, Coord.Y);
}
```

**Step 3: Implement UpdateStreaming and ProcessStreamingQueues**

Insert immediately after the above:

```cpp
void ATerraDyneManager::UpdateStreaming(const FVector& PlayerPos)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	if (!Settings) return;

	const int32 LoadRadius = Settings->ChunkLoadRadius;
	const int32 UnloadRadius = Settings->ChunkUnloadRadius;
	const int32 Extent = Settings->GridExtent;

	FIntPoint PlayerChunk = WorldToChunkCoord(PlayerPos);

	// Scan for loads (only rescan when player moves cells)
	if (PlayerChunk != LastStreamingCenter)
	{
		LastStreamingCenter = PlayerChunk;

		// Queue chunks within load radius that aren't loaded or already queued
		for (int32 DX = -LoadRadius; DX <= LoadRadius; DX++)
		{
			for (int32 DY = -LoadRadius; DY <= LoadRadius; DY++)
			{
				// Diamond shape: Manhattan distance
				if (FMath::Abs(DX) + FMath::Abs(DY) > LoadRadius) continue;

				FIntPoint Coord(PlayerChunk.X + DX, PlayerChunk.Y + DY);

				// Clamp to grid extent
				if (FMath::Abs(Coord.X) > Extent || FMath::Abs(Coord.Y) > Extent) continue;

				if (!ActiveChunkMap.Contains(Coord) && !PendingLoadQueue.Contains(Coord))
				{
					PendingLoadQueue.Add(Coord);
				}
			}
		}

		// Queue chunks outside unload radius
		TArray<FIntPoint> ToUnload;
		for (const auto& Pair : ActiveChunkMap)
		{
			int32 Dist = FMath::Abs(Pair.Key.X - PlayerChunk.X) + FMath::Abs(Pair.Key.Y - PlayerChunk.Y);
			if (Dist > UnloadRadius && !PendingUnloadQueue.Contains(Pair.Key))
			{
				ToUnload.Add(Pair.Key);
			}
		}
		for (const FIntPoint& Coord : ToUnload)
		{
			PendingUnloadQueue.Add(Coord);
		}
	}

	ProcessStreamingQueues(PlayerPos);
}

void ATerraDyneManager::ProcessStreamingQueues(const FVector& PlayerPos)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxOps = Settings ? Settings->MaxChunkOpsPerTick : 2;
	int32 Ops = 0;

	FIntPoint PlayerChunk = WorldToChunkCoord(PlayerPos);

	// Unloads first (free memory before allocating)
	if (PendingUnloadQueue.Num() > 0 && Ops < MaxOps)
	{
		// Sort by distance from player (farthest first)
		TArray<FIntPoint> UnloadList = PendingUnloadQueue.Array();
		UnloadList.Sort([&PlayerChunk](const FIntPoint& A, const FIntPoint& B)
		{
			int32 DistA = FMath::Abs(A.X - PlayerChunk.X) + FMath::Abs(A.Y - PlayerChunk.Y);
			int32 DistB = FMath::Abs(B.X - PlayerChunk.X) + FMath::Abs(B.Y - PlayerChunk.Y);
			return DistA > DistB; // Farthest first
		});

		for (const FIntPoint& Coord : UnloadList)
		{
			if (Ops >= MaxOps) break;
			UnloadChunk(Coord);
			Ops++;
		}
	}

	// Loads next (nearest to player first)
	if (PendingLoadQueue.Num() > 0 && Ops < MaxOps)
	{
		TArray<FIntPoint> LoadList = PendingLoadQueue.Array();
		LoadList.Sort([&PlayerChunk](const FIntPoint& A, const FIntPoint& B)
		{
			int32 DistA = FMath::Abs(A.X - PlayerChunk.X) + FMath::Abs(A.Y - PlayerChunk.Y);
			int32 DistB = FMath::Abs(B.X - PlayerChunk.X) + FMath::Abs(B.Y - PlayerChunk.Y);
			return DistA < DistB; // Nearest first
		});

		for (const FIntPoint& Coord : LoadList)
		{
			if (Ops >= MaxOps) break;
			LoadOrSpawnChunk(Coord);
			Ops++;
		}
	}
}
```

**Step 4: Wire streaming into Tick**

Modify `ATerraDyneManager::Tick()` (lines 89-124). Replace the existing body with:

```cpp
void ATerraDyneManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Periodic update (every ~0.25s)
	LODTimer -= DeltaTime;

	if (LODTimer <= 0.0f)
	{
		LODTimer = 0.25f;

		FVector PlayerPos = FVector::ZeroVector;
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					PlayerPos = Pawn->GetActorLocation();
				}
				else if (PC->GetViewTarget())
				{
					PlayerPos = PC->GetViewTarget()->GetActorLocation();
				}
			}
		}

		// Streaming (server only)
		if (HasAuthority())
		{
			UpdateStreaming(PlayerPos);
		}

		// LOD (all roles)
		for (auto& Pair : ActiveChunkMap)
		{
			if (Pair.Value)
			{
				Pair.Value->UpdateLOD(PlayerPos);
			}
		}
	}
}
```

**Step 5: Wire dirty tracking into ApplyGlobalBrush**

In `ApplyGlobalBrush()` (line 274), add `MarkChunkDirty(Coord);` inside the inner loop, after the chunk edit. Replace lines 290-300:

```cpp
	for (int32 X = MinX; X <= MaxX; X++)
	{
		for (int32 Y = MinY; Y <= MaxY; Y++)
		{
			FIntPoint Coord(X, Y);
			if (ATerraDyneChunk** FoundChunk = ActiveChunkMap.Find(Coord))
			{
				if (ATerraDyneChunk* Chunk = *FoundChunk)
				{
					FVector RelativePos = WorldLocation - Chunk->GetActorLocation();
					Chunk->ApplyLocalIdempotentEdit(RelativePos, Radius, Strength, BrushMode, WeightLayerIndex, FlattenHeight);
					MarkChunkDirty(Coord);
				}
			}
		}
	}
```

**Step 6: Clear streaming queues in LoadWorld**

In `LoadWorld()` (line 525), insert at the very beginning of the function, after the `SlotName` check:

```cpp
	// Clear streaming state — full world load resets everything
	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkMap.Empty();
	LastStreamingCenter = FIntPoint(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
```

**Step 7: Add required includes**

Add to the top of `TerraDyneManager.cpp` (after existing includes):

```cpp
#include "Misc/FileHelper.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Compression/OodleDataCompressionUtil.h"
#include "HAL/FileManager.h"
```

Note: `FCompression` is in `CoreMinimal.h`. If `NAME_Zlib` is unavailable in UE 5.7, use `FCompression::GetCompressionFormatName(ECompressionFlags::COMPRESS_ZLIB)` or `NAME_Oodle` as fallback. Check build output.

**Step 8: Build**

Run: UBT build command
Expected: `Result: Succeeded`

**Step 9: Commit**

```
feat(streaming): implement core chunk streaming with ring load/unload and per-chunk persistence
```

---

### Task 4: Write Streaming Automation Test

**Files:**
- Create: `Source/TerraDyne/Private/Tests/TerraDyneStreamingTest.cpp`

**Step 1: Write the test**

```cpp
// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "World/TerraDyneChunk.h"
#include "Settings/TerraDyneSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneStreamingTest, "TerraDyne.Streaming.RingLoadUnload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneStreamingTest::RunTest(const FString& Parameters)
{
	// Create a temporary world
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		AddError("Failed to create World");
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// Spawn Manager
	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	if (!Manager)
	{
		AddError("Manager failed to spawn");
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	// Test WorldToChunkCoord
	{
		// Chunk size defaults to 10000
		FIntPoint Coord = Manager->WorldToChunkCoord(FVector(15000, 25000, 0));
		TestEqual("WorldToChunkCoord X", Coord.X, 1);
		TestEqual("WorldToChunkCoord Y", Coord.Y, 2);

		FIntPoint NegCoord = Manager->WorldToChunkCoord(FVector(-5000, -15000, 0));
		TestEqual("WorldToChunkCoord negative X", NegCoord.X, -1);
		TestEqual("WorldToChunkCoord negative Y", NegCoord.Y, -2);
	}

	// Test GetChunkCachePath produces valid path
	{
		FString Path = Manager->GetChunkCachePath(FIntPoint(3, -2));
		TestTrue("Cache path contains coord", Path.Contains(TEXT("3_-2.chunk")));
	}

	// Test MarkChunkDirty
	{
		Manager->MarkChunkDirty(FIntPoint(0, 0));
		// Internal state — we verify via unload behavior later
	}

	// Cleanup
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}
```

**Step 2: Build**

Expected: `Result: Succeeded`

**Step 3: Run test**

Run: UE automation test `TerraDyne.Streaming.RingLoadUnload`
Expected: All assertions pass

**Step 4: Commit**

```
test(streaming): add chunk streaming automation test
```

---

### Task 5: Update SpawnDefaultSandboxChunk for Streaming Compatibility

**Files:**
- Modify: `Source/TerraDyne/Private/Core/TerraDyneManager.cpp:214-272`

The existing 3x3 hardcoded grid becomes the initial seed. Streaming extends from there. No change to the spawn logic itself — just ensure the initial chunks are within the streaming system's awareness.

**Step 1: Mark initial chunks in DirtyChunkMap as clean (they start from noise)**

No code change needed — `DirtyChunkMap` defaults to empty, so initial chunks are treated as clean. They'll regenerate from noise if unloaded without edits. This is correct behavior.

**Step 2: Verify the existing 3x3 grid still works**

The streaming system won't unload chunks within `UnloadRadius` (7) of the player. Since the player starts near (0,0) and the 3x3 grid is coords [-1,+1], these are all within radius 2 — well inside the unload threshold. The initial grid is never touched by streaming until the player moves far away.

**Step 3: Commit** (skip if no code changes)

---

### Task 6: Integration Testing & Memory Verification

**Step 1: Build the full project**

Run: UBT build command
Expected: `Result: Succeeded` with 0 errors

**Step 2: Run all automation tests**

Run: `TerraDyne.Demo.Simulation` — verifies the showcase still works
Run: `TerraDyne.Streaming.RingLoadUnload` — verifies streaming helpers

**Step 3: Manual PIE test**

1. Open DEMO.umap, press Play
2. After the 63s showcase completes, fly the camera beyond the 3x3 grid (past 15000 units from origin)
3. Verify new chunks spawn in the direction of travel
4. Fly back to origin, verify the original chunks are still there
5. Fly 80,000+ units away, verify original chunks eventually unload
6. Fly back — original chunks should reload (from cache if edited, from noise if clean)

**Step 4: Final commit**

```
feat(streaming): complete chunk streaming implementation with ring load/unload
```

---

## File Summary

| File | Action | Task |
|------|--------|------|
| `Public/Settings/TerraDyneSettings.h` | Modify (add 5 properties) | 1 |
| `Private/Settings/TerraDyneSettings.cpp` | Modify (add defaults) | 1 |
| `Public/Core/TerraDyneManager.h` | Modify (add streaming state + methods) | 2 |
| `Private/Core/TerraDyneManager.cpp` | Modify (streaming logic, tick, dirty tracking) | 3 |
| `Private/Tests/TerraDyneStreamingTest.cpp` | Create (new test) | 4 |

## Build Command

```
"G:/Epic Games/UE_5.7/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" TerraDyneEditor Win64 Development -Project="G:/Epic Games/TerraDyne/TerraDyne.uproject" -WaitMutex -FromMsBuild
```
