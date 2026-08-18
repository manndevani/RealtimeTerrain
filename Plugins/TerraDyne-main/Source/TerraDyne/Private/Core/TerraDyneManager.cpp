// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneSaveGame.h"
#include "Core/TerraDyneWorldPreset.h"
#include "Settings/TerraDyneSettings.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Engine/StaticMeshActor.h"


#include "Core/TerraDyneManagerInternal.h"

ATerraDyneManager::ATerraDyneManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	GlobalChunkSize = 10000.0f;
	ChunkClass = ATerraDyneChunk::StaticClass();
	bAutoImportAtRuntime = false;
	bSpawnDefaultChunksOnBeginPlay = false;
	bSetupDefaultLightingOnBeginPlay = false;
	bSpawnShowcaseOnBeginPlay = false;
	PopulationMaintenanceTimer = 0.25f;
}

void ATerraDyneManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATerraDyneManager, GlobalChunkSize);
	DOREPLIFETIME(ATerraDyneManager, ChunkClass);
	DOREPLIFETIME(ATerraDyneManager, ActiveLayer);
	DOREPLIFETIME(ATerraDyneManager, MasterMaterial);
	DOREPLIFETIME(ATerraDyneManager, LandscapeMigrationState);
	DOREPLIFETIME(ATerraDyneManager, ProceduralWorldSettings);
	DOREPLIFETIME(ATerraDyneManager, BiomeOverlays);
	DOREPLIFETIME(ATerraDyneManager, AISpawnZones);
	DOREPLIFETIME(ATerraDyneManager, BuildPermissionZones);
}

void ATerraDyneManager::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("TerraDyneManager: Starting initialization..."));

	// Register with subsystem
	if (UTerraDyneSubsystem* Subsystem = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
	{
		Subsystem->RegisterManager(this);
	}

	if (WorldPreset)
	{
		ApplyWorldPreset(WorldPreset);
	}

	// Load materials (needed on all roles for rendering)
	LoadMaterials();

	// Capture any pre-placed chunks before bootstrap logic runs.
	RebuildChunkMap();
	if (LandscapeMigrationState.bWasImportedFromLandscape && ImportedChunkCoords.Num() == 0)
	{
		for (const auto& Pair : ActiveChunkMap)
		{
			ImportedChunkCoords.Add(Pair.Key);
		}
	}
	ApplyMaterialToActiveChunks();
	RebuildWorldStateIndices();
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			EnsureChunkFrameworkState(Pair.Key, Pair.Value);
		}
	}

	// Only the server spawns world actors — clients receive them via replication
	if (HasAuthority())
	{
		bool bChunkMapDirty = false;
#if WITH_EDITOR
		if (bAutoImportAtRuntime && TargetLandscapeSource)
		{
			ImportInternal(TargetLandscapeSource, LandscapeMigrationOptions);
			bChunkMapDirty = true;
		}
		else
#endif
		if (bSpawnDefaultChunksOnBeginPlay && ActiveChunkMap.Num() == 0)
		{
			SpawnDefaultSandboxChunk();
			bChunkMapDirty = true;
		}

		if (bSetupDefaultLightingOnBeginPlay)
		{
			SetupLighting();
		}

		if (bSpawnShowcaseOnBeginPlay)
		{
			TArray<AActor*> ExistingOrch;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneOrchestrator::StaticClass(), ExistingOrch);
			if (ExistingOrch.Num() == 0)
			{
				GetWorld()->SpawnActor<ATerraDyneOrchestrator>(ATerraDyneOrchestrator::StaticClass(), FVector(0, 0, 5000), FRotator::ZeroRotator);
			}
		}

		if (bChunkMapDirty)
		{
			RebuildChunkMap();
			ApplyMaterialToActiveChunks();
		}

		RefreshPopulationForLoadedChunks();
	}

	// Debug: List all chunks
	UE_LOG(LogTemp, Log, TEXT("Active chunks: %d"), ActiveChunkMap.Num());
	
	UE_LOG(LogTemp, Log, TEXT("TerraDyneManager: Initialization complete"));
}

void ATerraDyneManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority())
	{
		PopulationMaintenanceTimer -= DeltaTime;
		if (PopulationMaintenanceTimer <= 0.0f)
		{
			TickPopulationState(0.25f);
			PopulationMaintenanceTimer = 0.25f;
		}
	}

	// Periodic update (every ~0.25s)
	LODTimer -= DeltaTime;

	if (LODTimer <= 0.0f)
	{
		LODTimer = 0.25f;

		// Gather all player positions
		TArray<FVector> PlayerPositions;
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (!PC) continue;

				FVector Pos = FVector::ZeroVector;
				if (PC->PlayerCameraManager)
				{
					Pos = PC->PlayerCameraManager->GetCameraLocation();
				}
				else if (APawn* Pawn = PC->GetPawn())
				{
					Pos = Pawn->GetActorLocation();
				}
				else if (PC->GetViewTarget())
				{
					Pos = PC->GetViewTarget()->GetActorLocation();
				}
				PlayerPositions.Add(Pos);
			}
		}

		if (PlayerPositions.Num() == 0)
		{
			PlayerPositions.Add(FVector::ZeroVector);
		}

		// Streaming (server only) — uses union of all player positions
		if (HasAuthority())
		{
			UpdateStreaming(PlayerPositions);
		}

		// LOD (all roles) — each chunk uses nearest player
		for (auto& Pair : ActiveChunkMap)
		{
			if (!Pair.Value) continue;

			float MinDistSq = MAX_FLT;
			FVector NearestPos = PlayerPositions[0];
			for (const FVector& Pos : PlayerPositions)
			{
				float DistSq = FVector::DistSquared(Pos, Pair.Value->GetActorLocation());
				if (DistSq < MinDistSq)
				{
					MinDistSq = DistSq;
					NearestPos = Pos;
				}
			}
			Pair.Value->UpdateLOD(NearestPos);
		}
	}
}

void ATerraDyneManager::LoadMaterials()
{
	if (bMaterialsLoaded) return;

	UE_LOG(LogTemp, Log, TEXT("Loading TerraDyne materials..."));

	// [N-2] Guard: CDO lookup can return nullptr during hot-reload edge cases
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("TerraDyneSettings CDO not found"));
		return;
	}

	// 1. Try explicit settings path first.
	if (!MasterMaterial && Settings->MasterMaterialPath.IsValid())
	{
		MasterMaterial = Cast<UMaterialInterface>(Settings->MasterMaterialPath.TryLoad());
	}

	// Final fallback
	if (MasterMaterial)
	{
		UE_LOG(LogTemp, Log, TEXT("MasterMaterial loaded: %s"), *MasterMaterial->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load MasterMaterial, using Engine Basic Shape"));
		MasterMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	
	// Height Brush
	if (!HeightBrushMaterial && Settings->HeightBrushMaterialPath.IsValid())
	{
		HeightBrushMaterial = Cast<UMaterialInterface>(Settings->HeightBrushMaterialPath.TryLoad());
	}

	bMaterialsLoaded = true;
}

void ATerraDyneManager::SetupLighting()
{
	// [N-4] Early-return if a DirectionalLight already exists — prevents duplicates on re-entry
	TArray<AActor*> Lights;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADirectionalLight::StaticClass(), Lights);
	if (Lights.Num() > 0) return;

	ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), 
		FVector(0, 0, 5000), 
		FRotator(-45, -45, 0));
	
	if (Sun && Sun->GetLightComponent())
	{
		Sun->GetLightComponent()->SetIntensity(10.0f);
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	}

	ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(), 
		FVector::ZeroVector, 
		FRotator::ZeroRotator);
	
	if (Sky && Sky->GetLightComponent())
	{
		Sky->GetLightComponent()->SetIntensity(1.0f);
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->bRealTimeCapture = true;
	}
}

void ATerraDyneManager::SpawnDefaultSandboxChunk()
{
	UE_LOG(LogTemp, Log, TEXT("Spawning 3x3 chunk grid..."));
	
	for (int32 X = -1; X <= 1; X++)
	{
		for (int32 Y = -1; Y <= 1; Y++)
		{
			FIntPoint GridCoord(X, Y);

			ATerraDyneChunk* Chunk = SpawnConfiguredChunk(GridCoord);
			if (Chunk)
			{
				// Debug box to show chunk bounds
				DrawDebugBox(GetWorld(), Chunk->GetActorLocation(), FVector(GlobalChunkSize/2, GlobalChunkSize/2, 1000), 
					FColor::Green, true, 10.0f, 0, 10.0f);
				
				UE_LOG(LogTemp, Log, TEXT("Spawned chunk [%d,%d] at %s"), X, Y, *Chunk->GetActorLocation().ToString());
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("FAILED to spawn chunk [%d,%d]"), X, Y);
			}
		}
	}
}

void ATerraDyneManager::ApplyGlobalBrush(FVector WorldLocation, float Radius, float Strength,
	ETerraDyneBrushMode BrushMode, float FlattenHeight)
{
	UE_LOG(LogTemp, Verbose, TEXT("ApplyGlobalBrush: Loc=%s R=%.0f S=%.0f Mode=%d"),
		*WorldLocation.ToString(), Radius, Strength, (int32)BrushMode);

	if (ActiveChunkMap.Num() == 0)
	{
		RebuildChunkMap();
	}

	const FBox2D BrushBounds(
		FVector2D(WorldLocation.X - Radius, WorldLocation.Y - Radius),
		FVector2D(WorldLocation.X + Radius, WorldLocation.Y + Radius));

	TArray<FIntPoint> ChangedCoords;
	for (ATerraDyneChunk* Chunk : GetChunksInBoxXY(BrushBounds))
	{
		if (Chunk)
		{
			FVector RelativePos = WorldLocation - Chunk->GetActorLocation();
			Chunk->ApplyLocalIdempotentEdit(RelativePos, Radius, Strength, BrushMode, FlattenHeight);
			MarkChunkDirty(Chunk->GridCoordinate);
			ChangedCoords.AddUnique(Chunk->GridCoordinate);
		}
	}

	if (ChangedCoords.Num() > 0)
	{
		BroadcastTerrainChanged(ChangedCoords);
		BroadcastFoliageChanged(ChangedCoords);
		if (HasAuthority())
		{
			for (const FIntPoint& Coord : ChangedCoords)
			{
				SyncPopulationForChunk(Coord);
			}
		}
	}
}

ATerraDyneChunk* ATerraDyneManager::GetChunkAtLocation(FVector WorldLocation) const
{
	if (ActiveChunkMap.Num() == 0)
	{
		const_cast<ATerraDyneManager*>(this)->RebuildChunkMap();
	}
	if (ATerraDyneChunk* const* Chunk = ActiveChunkMap.Find(WorldToChunkCoord(WorldLocation))) return *Chunk;

	const FVector2D Point(WorldLocation.X, WorldLocation.Y);
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			const FBox2D Bounds = GetChunkBoundsXY(Pair.Value);
			if (Bounds.bIsValid && Bounds.IsInside(Point))
			{
				return Pair.Value;
			}
		}
	}
	return nullptr;
}

FBox2D ATerraDyneManager::GetChunkBoundsXY(const ATerraDyneChunk* Chunk) const
{
	if (!Chunk)
	{
		return FBox2D(ForceInit);
	}

	const FVector Origin = Chunk->GetActorLocation();
	const float Half = (Chunk->WorldSize > 0.0f ? Chunk->WorldSize : GlobalChunkSize) * 0.5f;
	return FBox2D(
		FVector2D(Origin.X - Half, Origin.Y - Half),
		FVector2D(Origin.X + Half, Origin.Y + Half));
}

TArray<ATerraDyneChunk*> ATerraDyneManager::GetChunksInBoxXY(const FBox2D& WorldAreaXY) const
{
	TArray<ATerraDyneChunk*> Result;
	if (!WorldAreaXY.bIsValid)
	{
		return Result;
	}

	if (ActiveChunkMap.Num() == 0)
	{
		const_cast<ATerraDyneManager*>(this)->RebuildChunkMap();
	}

	for (const TPair<FIntPoint, ATerraDyneChunk*>& Pair : ActiveChunkMap)
	{
		ATerraDyneChunk* Chunk = Pair.Value;
		if (!Chunk)
		{
			continue;
		}

		const FBox2D ChunkBox = GetChunkBoundsXY(Chunk);
		if (ChunkBox.bIsValid && ChunkBox.Intersect(WorldAreaXY))
		{
			Result.Add(Chunk);
		}
	}

	return Result;
}

bool ATerraDyneManager::GetNaturalTerrainHeightAtXY(FVector2D WorldXY, float& OutWorldZ) const
{
	const FVector Probe(WorldXY.X, WorldXY.Y, 0.0f);
	const ATerraDyneChunk* Chunk = GetChunkAtLocation(Probe);
	if (!Chunk)
	{
		return false;
	}

	const FVector ChunkOrigin = Chunk->GetActorLocation();
	OutWorldZ = ChunkOrigin.Z + Chunk->GetNaturalHeightAtLocation(Probe - ChunkOrigin);
	return true;
}

void ATerraDyneManager::RegisterBuilding(ATerraDyneBuildingActor* Building)
{
	if (!Building)
	{
		return;
	}

	RegisteredBuildings.RemoveAll([](const TWeakObjectPtr<ATerraDyneBuildingActor>& Entry)
	{
		return !Entry.IsValid();
	});

	RegisteredBuildings.AddUnique(Building);
}

void ATerraDyneManager::UnregisterBuilding(ATerraDyneBuildingActor* Building)
{
	if (!Building)
	{
		return;
	}

	// Recompute the ground it was standing on before dropping it, otherwise its pad stays stamped
	// into every chunk it overlapped for as long as those chunks remain loaded.
	const FBox2D Area = Building->GetFootprintBoundsWorld(Building->PadFalloff);

	RegisteredBuildings.RemoveAll([Building](const TWeakObjectPtr<ATerraDyneBuildingActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Building;
	});

	RefreshBuildingPads(Area);
}

void ATerraDyneManager::RefreshBuildingPads(const FBox2D& WorldAreaXY)
{
	if (!WorldAreaXY.bIsValid)
	{
		return;
	}

	TArray<FIntPoint> ChangedCoords;
	for (ATerraDyneChunk* Chunk : GetChunksInBoxXY(WorldAreaXY))
	{
		RefreshBuildingPadsForChunk(Chunk);
		if (Chunk)
		{
			ChangedCoords.Add(Chunk->GridCoordinate);
			MarkChunkDirty(Chunk->GridCoordinate);
		}
	}

	if (ChangedCoords.Num() > 0)
	{
		BroadcastTerrainChanged(ChangedCoords);
	}

	if (HasAuthority())
	{
		Multicast_RefreshBuildingPads(WorldAreaXY.Min, WorldAreaXY.Max);
	}
}

void ATerraDyneManager::Multicast_RefreshBuildingPads_Implementation(FVector2D MinXY, FVector2D MaxXY)
{
	if (!HasAuthority())
	{
		RefreshBuildingPads(FBox2D(MinXY, MaxXY));
	}
}

void ATerraDyneManager::RefreshBuildingPadsForChunk(ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	const FBox2D ChunkBox = GetChunkBoundsXY(Chunk);
	if (!ChunkBox.bIsValid)
	{
		return;
	}

	// Which buildings actually reach this chunk. Gathered first so the common case — no buildings
	// anywhere near — costs one bounds test each and then stops, rather than clearing and
	// recomposing a chunk that has nothing to do with any building.
	TArray<ATerraDyneBuildingActor*> Overlapping;
	for (const TWeakObjectPtr<ATerraDyneBuildingActor>& Entry : RegisteredBuildings)
	{
		ATerraDyneBuildingActor* Building = Entry.Get();
		if (!Building || !Building->bFlattenTerrain)
		{
			continue;
		}

		const FBox2D BuildingBox = Building->GetFootprintBoundsWorld(Building->PadFalloff);
		if (BuildingBox.bIsValid && BuildingBox.Intersect(ChunkBox))
		{
			Overlapping.Add(Building);
		}
	}

	const bool bHadPad = Chunk->HasBuildingPad();
	if (Overlapping.Num() == 0 && !bHadPad)
	{
		return;
	}

	Chunk->ClearBuildingPad();

	const FVector ChunkOrigin = Chunk->GetActorLocation();
	for (ATerraDyneBuildingActor* Building : Overlapping)
	{
		const TArray<FVector2D> WorldPolygon = Building->GetFootprintPolygonWorld();
		if (WorldPolygon.Num() < 3)
		{
			continue;
		}

		TArray<FVector2D> LocalPolygon;
		LocalPolygon.Reserve(WorldPolygon.Num());
		for (const FVector2D& P : WorldPolygon)
		{
			LocalPolygon.Emplace(P.X - ChunkOrigin.X, P.Y - ChunkOrigin.Y);
		}

		const float TargetLocalZ = Building->ResolvePadTargetWorldZ() - ChunkOrigin.Z;
		Chunk->StampBuildingPad(LocalPolygon, TargetLocalZ, Building->PadFalloff);
	}

	Chunk->RecomputeHeightFromLayers();
	Chunk->NotifyHeightBufferChanged();
	Chunk->MarkLayerWeightsDirty();
}

TArray<ATerraDyneChunk*> ATerraDyneManager::GetChunksInRadius(FVector WorldLocation, float Radius)
{
	TArray<ATerraDyneChunk*> Result;
	const float RadiusWorld = Radius + GlobalChunkSize * 0.5f;
	const float ProbeInset = 0.01f;
	const FIntPoint MinCoord = WorldToChunkCoord(WorldLocation - FVector(RadiusWorld, RadiusWorld, 0.0f));
	const FIntPoint MaxCoord = WorldToChunkCoord(
		WorldLocation + FVector(RadiusWorld - ProbeInset, RadiusWorld - ProbeInset, 0.0f));
	for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
	{
		for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
		{
			if (ATerraDyneChunk** Found = ActiveChunkMap.Find(FIntPoint(X, Y)))
			{
				if (*Found) Result.Add(*Found);
			}
		}
	}
	return Result;
}

ATerraDyneChunk* ATerraDyneManager::GetChunkAtCoord(FIntPoint Coord) const
{
	ATerraDyneChunk* const* Found = ActiveChunkMap.Find(Coord);
	return Found ? *Found : nullptr;
}

void ATerraDyneManager::RegisterChunk(ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	if (ATerraDyneChunk** ExistingChunk = ActiveChunkMap.Find(Chunk->GridCoordinate))
	{
		if (*ExistingChunk && *ExistingChunk != Chunk)
		{
			UE_LOG(LogTemp, Warning, TEXT("Duplicate chunk detected at [%d,%d] — replacing stale instance %s with %s"),
				Chunk->GridCoordinate.X, Chunk->GridCoordinate.Y, *(*ExistingChunk)->GetName(), *Chunk->GetName());
			if (!(*ExistingChunk)->HasAuthority())
			{
				(*ExistingChunk)->Destroy();
			}
		}
	}

	ActiveChunkMap.Add(Chunk->GridCoordinate, Chunk);
	Chunk->SetMaterial(MasterMaterial);
	Chunk->SetTransferredFoliageFollowsTerrain(LandscapeMigrationState.bTransferredFoliageFollowsTerrain);
	EnsureChunkFrameworkState(Chunk->GridCoordinate, Chunk);
	// A chunk streaming back in under an existing building has to get its pad re-stamped, or the
	// ground reverts to its natural shape and the building floats. No-op when nothing overlaps.
	RefreshBuildingPadsForChunk(Chunk);
	if (HasAuthority())
	{
		SyncPopulationForChunk(Chunk->GridCoordinate);
	}
}

void ATerraDyneManager::UnregisterChunk(ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	if (ATerraDyneChunk** Found = ActiveChunkMap.Find(Chunk->GridCoordinate))
	{
		if (*Found == Chunk)
		{
			ActiveChunkMap.Remove(Chunk->GridCoordinate);
		}
	}
}

void ATerraDyneManager::BeginStroke(FVector WorldLocation, float Radius, APlayerController* Controller)
{
	if (PendingStroke.IsSet() && PendingStrokeOwner != Controller)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("BeginStroke: overwriting pending stroke from %s with stroke from %s — previous stroke discarded"),
			*PendingStrokeOwner->GetName(), *Controller->GetName());
	}
	PendingStroke.Reset();
	PendingStrokeOwner = Controller;

	FTerraDyneUndoEntry Entry;
	for (ATerraDyneChunk* Chunk : GetChunksInRadius(WorldLocation, Radius))
	{
		FTerraDyneChunkSnapshot Snap;
		Snap.Coordinate = Chunk->GridCoordinate;
		Snap.SculptBuffer = Chunk->SculptBuffer;
		Entry.Snapshots.Add(Chunk->GridCoordinate, MoveTemp(Snap));
	}
	PendingStroke = MoveTemp(Entry);
}

void ATerraDyneManager::CommitStroke(APlayerController* Controller)
{
	if (!PendingStroke.IsSet() || PendingStrokeOwner != Controller) return;

	TArray<FTerraDyneUndoEntry>& Stack = UndoStacks.FindOrAdd(Controller);
	Stack.Add(MoveTemp(PendingStroke.GetValue()));
	PendingStroke.Reset();
	PendingStrokeOwner = nullptr;

	int32 MaxHistory = 20;
	if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		MaxHistory = S->MaxUndoHistory;
	}
	if (Stack.Num() > MaxHistory)
		Stack.RemoveAt(0, Stack.Num() - MaxHistory);

	// New stroke clears redo for this player
	RedoStacks.Remove(Controller);
}

void ATerraDyneManager::RestoreSnapshots(const FTerraDyneUndoEntry& Entry)
{
	TArray<FIntPoint> RestoredCoords;
	for (const auto& Pair : Entry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		const FTerraDyneChunkSnapshot& Snap = Pair.Value;

		Chunk->SculptBuffer = Snap.SculptBuffer;
		// Undo restores the sculpt layer only. The building pad is derived state and is left
		// untouched, so undoing a brush stroke under a building cannot dig a hole through its pad.
		Chunk->RecomputeHeightFromLayers();

		const int32 MeshSamples = Chunk->Resolution * Chunk->Resolution;
		if (MeshSamples > 0 && Chunk->HeightBuffer.Num() == MeshSamples)
		{
			Chunk->RebuildPhysicsMesh();
			Chunk->RequestTransferredFoliageRefresh();
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("RestoreSnapshots: chunk %s has invalid terrain sample count (%d vs %d); skipping rebuild."),
				*Snap.Coordinate.ToString(), Chunk->HeightBuffer.Num(), MeshSamples);
		}

		RestoredCoords.AddUnique(Pair.Key);
	}

	if (RestoredCoords.Num() > 0)
	{
		BroadcastTerrainChanged(RestoredCoords);
		BroadcastFoliageChanged(RestoredCoords);
		if (HasAuthority())
		{
			for (const FIntPoint& Coord : RestoredCoords)
			{
				SyncPopulationForChunk(Coord);
			}
		}
	}
}

void ATerraDyneManager::Undo(APlayerController* Controller)
{
	TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
	if (!Stack || Stack->Num() == 0) return;

	FTerraDyneUndoEntry UndoEntry = Stack->Pop();

	// Snapshot current state for redo
	FTerraDyneUndoEntry RedoEntry;
	for (const auto& Pair : UndoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FTerraDyneChunkSnapshot CurrentSnap;
		CurrentSnap.Coordinate = Chunk->GridCoordinate;
		CurrentSnap.SculptBuffer = Chunk->SculptBuffer;
		RedoEntry.Snapshots.Add(Pair.Key, MoveTemp(CurrentSnap));
	}
	RedoStacks.FindOrAdd(Controller).Add(MoveTemp(RedoEntry));

	RestoreSnapshots(UndoEntry);

	// Multicast affected chunks to remote clients
	for (const auto& Pair : UndoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		SendChunkSculptSync(Chunk);
	}
}

void ATerraDyneManager::Redo(APlayerController* Controller)
{
	TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
	if (!Stack || Stack->Num() == 0) return;

	FTerraDyneUndoEntry RedoEntry = Stack->Pop();

	// Snapshot current state back to undo
	FTerraDyneUndoEntry UndoEntry;
	for (const auto& Pair : RedoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FTerraDyneChunkSnapshot CurrentSnap;
		CurrentSnap.Coordinate = Chunk->GridCoordinate;
		CurrentSnap.SculptBuffer = Chunk->SculptBuffer;
		UndoEntry.Snapshots.Add(Pair.Key, MoveTemp(CurrentSnap));
	}
	UndoStacks.FindOrAdd(Controller).Add(MoveTemp(UndoEntry));

	RestoreSnapshots(RedoEntry);

	// Multicast affected chunks to remote clients
	for (const auto& Pair : RedoEntry.Snapshots)
	{
		ATerraDyneChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		SendChunkSculptSync(Chunk);
	}
}

void ATerraDyneManager::ApplyGlobalNoise(float Strength, float Frequency, float Seed)
{
	const float SafeFrequency = (Frequency > KINDA_SMALL_NUMBER) ? Frequency : 0.001f;
	TArray<FIntPoint> ChangedCoords;

	for (auto& Pair : ActiveChunkMap)
	{
		ATerraDyneChunk* Chunk = Pair.Value;
		if (!Chunk || Chunk->Resolution < 2)
		{
			continue;
		}

		const int32 Resolution = Chunk->Resolution;
		const float WorldSize = Chunk->WorldSize > 0.0f ? Chunk->WorldSize : GlobalChunkSize;
		const FVector ChunkOrigin(Chunk->GridCoordinate.X * WorldSize, Chunk->GridCoordinate.Y * WorldSize, 0.0);
		const float ZScale = Chunk->ZScale > 0.0f ? Chunk->ZScale : 8000.0f;
		const float StrengthNorm = Strength / ZScale;

		const int32 NumVerts = Resolution * Resolution;
		if (Chunk->DetailBuffer.Num() != NumVerts)
		{
			Chunk->DetailBuffer.SetNumZeroed(NumVerts);
		}

		for (int32 Y = 0; Y < Resolution; Y++)
		{
			for (int32 X = 0; X < Resolution; X++)
			{
				const double WorldX = ChunkOrigin.X + ((double(X) / (Resolution - 1)) - 0.5) * WorldSize + (double)Seed * 1337.0;
				const double WorldY = ChunkOrigin.Y + ((double(Y) / (Resolution - 1)) - 0.5) * WorldSize + (double)Seed * 1337.0;

				const float NoiseVal = (FMath::PerlinNoise2D(FVector2D(WorldX * SafeFrequency, WorldY * SafeFrequency))) * StrengthNorm;
				const int32 Index = Y * Resolution + X;
				Chunk->DetailBuffer[Index] += NoiseVal;
			}
		}

		Chunk->RecomputeHeightFromLayers();
		Chunk->RebuildPhysicsMesh();
		Chunk->RequestTransferredFoliageRefresh();
		MarkChunkDirty(Chunk->GridCoordinate);
		ChangedCoords.Add(Chunk->GridCoordinate);
	}

	if (ChangedCoords.Num() > 0)
	{
		BroadcastTerrainChanged(ChangedCoords);
		BroadcastFoliageChanged(ChangedCoords);
	}

	if (HasAuthority())
	{
		Multicast_ApplyGlobalNoise(Strength, Frequency, Seed);
	}
}

void ATerraDyneManager::Multicast_ApplyGlobalNoise_Implementation(float Strength, float Frequency, float Seed)
{
	if (!HasAuthority())
	{
		ApplyGlobalNoise(Strength, Frequency, Seed);
	}
}

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

void ATerraDyneManager::SetAuthoredChunkCoordinates(
	const TArray<FIntPoint>& ChunkCoords,
	bool bWasImportedFromLandscape)
{
	ImportedChunkCoords.Empty();
	for (const FIntPoint& Coord : ChunkCoords)
	{
		ImportedChunkCoords.Add(Coord);
	}

	LandscapeMigrationState.bWasImportedFromLandscape =
		bWasImportedFromLandscape && ImportedChunkCoords.Num() > 0;
	LandscapeMigrationState.ImportedComponentCount = ImportedChunkCoords.Num();
	LandscapeMigrationState.ImportedChunkSize = GlobalChunkSize;
	LandscapeMigrationState.RuntimeManagerLocation = GetActorLocation();

	OnRep_LandscapeMigrationState();
}

void ATerraDyneManager::RebuildChunkMap()
{
	ActiveChunkMap.Reset();
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), FoundActors);
	for (AActor* Actor : FoundActors)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			if (Chunk->IsActorBeingDestroyed())
			{
				continue;
			}
			ActiveChunkMap.Add(Chunk->GridCoordinate, Chunk);
		}
	}

	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			EnsureChunkFrameworkState(Pair.Key, Pair.Value);
		}
	}
}

void ATerraDyneManager::ApplyMaterialToActiveChunks()
{
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			Pair.Value->SetMaterial(MasterMaterial);
		}
	}
}


void ATerraDyneManager::ClearUndoRedoState()
{
	UndoStacks.Reset();
	RedoStacks.Reset();
	PendingStroke.Reset();
	PendingStrokeOwner = nullptr;
}

bool ATerraDyneManager::IsImportedChunkCoord(FIntPoint Coord) const
{
	return ImportedChunkCoords.Contains(Coord);
}

bool ATerraDyneManager::ShouldAllowChunkCoord(FIntPoint Coord) const
{
	if (ImportedChunkCoords.Contains(Coord))
	{
		return true;
	}

	if (ImportedChunkCoords.Num() == 0)
	{
		if (ProceduralWorldSettings.bEnableInfiniteEdgeGrowth)
		{
			return true;
		}

		if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
		{
			return FMath::Abs(Coord.X) <= Settings->GridExtent && FMath::Abs(Coord.Y) <= Settings->GridExtent;
		}

		return true;
	}

	if (!ProceduralWorldSettings.bEnableSeededOutskirts)
	{
		return false;
	}

	if (ProceduralWorldSettings.bEnableInfiniteEdgeGrowth)
	{
		return true;
	}

	return GetDistanceFromAuthoredChunks(Coord) <= FMath::Max(0, ProceduralWorldSettings.ProceduralOutskirtsExtent);
}

int32 ATerraDyneManager::GetDistanceFromAuthoredChunks(FIntPoint Coord) const
{
	if (ImportedChunkCoords.Num() == 0)
	{
		return FMath::Max(FMath::Abs(Coord.X), FMath::Abs(Coord.Y));
	}

	int32 MinDistance = TNumericLimits<int32>::Max();
	for (const FIntPoint& AuthoredCoord : ImportedChunkCoords)
	{
		MinDistance = FMath::Min(
			MinDistance,
			FMath::Abs(Coord.X - AuthoredCoord.X) + FMath::Abs(Coord.Y - AuthoredCoord.Y));
	}

	return MinDistance == TNumericLimits<int32>::Max() ? 0 : MinDistance;
}

int32 ATerraDyneManager::MakeChunkSeed(FIntPoint Coord) const
{
	return static_cast<int32>(HashCombine(GetTypeHash(Coord), GetTypeHash(ProceduralWorldSettings.WorldSeed)));
}

void ATerraDyneManager::RebuildWorldStateIndices()
{
	PopulationEntryIndexById.Reset();
	for (int32 Index = 0; Index < PersistentPopulationEntries.Num(); ++Index)
	{
		if (!PersistentPopulationEntries[Index].PopulationId.IsValid())
		{
			PersistentPopulationEntries[Index].PopulationId = FGuid::NewGuid();
		}

		PopulationEntryIndexById.Add(PersistentPopulationEntries[Index].PopulationId, Index);
	}

	ProceduralChunkStateIndexByCoord.Reset();
	for (int32 Index = 0; Index < ProceduralChunkStates.Num(); ++Index)
	{
		ProceduralChunkStateIndexByCoord.Add(ProceduralChunkStates[Index].Coordinate, Index);
	}
}

FTerraDyneProceduralChunkState* ATerraDyneManager::FindProceduralChunkState(FIntPoint Coord)
{
	if (const int32* Index = ProceduralChunkStateIndexByCoord.Find(Coord))
	{
		return ProceduralChunkStates.IsValidIndex(*Index) ? &ProceduralChunkStates[*Index] : nullptr;
	}

	return nullptr;
}

const FTerraDyneProceduralChunkState* ATerraDyneManager::FindProceduralChunkState(FIntPoint Coord) const
{
	if (const int32* Index = ProceduralChunkStateIndexByCoord.Find(Coord))
	{
		return ProceduralChunkStates.IsValidIndex(*Index) ? &ProceduralChunkStates[*Index] : nullptr;
	}

	return nullptr;
}

FName ATerraDyneManager::ResolveBiomeForChunk(FIntPoint Coord, const ATerraDyneChunk* Chunk, TArray<FName>* OutOverlays) const
{
	FName SelectedBiome = ImportedChunkCoords.Contains(Coord) ? FName(TEXT("Authored")) : FName(TEXT("Outskirts"));
	int32 SelectedPriority = TNumericLimits<int32>::Lowest();
	const bool bAuthoredChunk = ImportedChunkCoords.Contains(Coord);
	const float ProceduralNoise = FRandomStream(MakeChunkSeed(Coord)).FRand();

	if (OutOverlays)
	{
		OutOverlays->Reset();
	}

	for (const FTerraDyneBiomeOverlay& Overlay : BiomeOverlays)
	{
		bool bMatches = false;

		// Authored chunks used to match on average paint weight; with paint layers removed there is
		// no per-chunk authored signal left, so only the procedural noise gate remains.
		if (!bAuthoredChunk && Overlay.bApplyToProceduralChunks)
		{
			bMatches = ProceduralNoise >= Overlay.ProceduralNoiseMin && ProceduralNoise <= Overlay.ProceduralNoiseMax;
		}

		if (!bMatches)
		{
			continue;
		}

		if (OutOverlays)
		{
			OutOverlays->AddUnique(Overlay.BiomeTag);
		}

		if (Overlay.Priority >= SelectedPriority)
		{
			SelectedPriority = Overlay.Priority;
			SelectedBiome = Overlay.BiomeTag;
		}
	}

	return SelectedBiome;
}

void ATerraDyneManager::EnsureChunkFrameworkState(FIntPoint Coord, ATerraDyneChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	FTerraDyneProceduralChunkState* State = FindProceduralChunkState(Coord);
	if (!State)
	{
		FTerraDyneProceduralChunkState NewState;
		NewState.Coordinate = Coord;
		NewState.ChunkSeed = MakeChunkSeed(Coord);
		NewState.bIsAuthoredChunk = ImportedChunkCoords.Contains(Coord);
		NewState.bGeneratedOutskirtsChunk = !NewState.bIsAuthoredChunk;
		NewState.DistanceFromAuthored = GetDistanceFromAuthoredChunks(Coord);
		NewState.PrimaryBiomeTag = ResolveBiomeForChunk(Coord, Chunk, &NewState.OverlayBiomeTags);

		const int32 NewIndex = ProceduralChunkStates.Add(MoveTemp(NewState));
		ProceduralChunkStateIndexByCoord.Add(Coord, NewIndex);
		State = &ProceduralChunkStates[NewIndex];
	}
	else
	{
		State->ChunkSeed = MakeChunkSeed(Coord);
		State->bIsAuthoredChunk = ImportedChunkCoords.Contains(Coord);
		State->bGeneratedOutskirtsChunk = !State->bIsAuthoredChunk;
		State->DistanceFromAuthored = GetDistanceFromAuthoredChunks(Coord);
		State->PrimaryBiomeTag = ResolveBiomeForChunk(Coord, Chunk, &State->OverlayBiomeTags);
	}

	Chunk->ProceduralSeed = State->ChunkSeed;
	Chunk->bIsAuthoredChunk = State->bIsAuthoredChunk;
	Chunk->PrimaryBiomeTag = State->PrimaryBiomeTag;
}

void ATerraDyneManager::SetMasterMaterial(UMaterialInterface* NewMaterial)
{
	if (!HasAuthority() || NewMaterial == MasterMaterial)
	{
		return;
	}

	MasterMaterial = NewMaterial;

	// Existing chunks are re-materialled now; chunks that stream in later read MasterMaterial in
	// SpawnChunk, so they pick this up on their own.
	ApplyMaterialToActiveChunks();
}

void ATerraDyneManager::OnRep_MasterMaterial()
{
	ApplyMaterialToActiveChunks();
}

void ATerraDyneManager::OnRep_LandscapeMigrationState()
{
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			Pair.Value->SetTransferredFoliageFollowsTerrain(LandscapeMigrationState.bTransferredFoliageFollowsTerrain);
		}
	}

	OnRep_FrameworkConfig();
}

void ATerraDyneManager::OnRep_FrameworkConfig()
{
	RebuildWorldStateIndices();
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			EnsureChunkFrameworkState(Pair.Key, Pair.Value);
		}
	}

	if (HasAuthority())
	{
		RefreshPopulationForLoadedChunks();
	}
}

FTerraDyneGPUStats ATerraDyneManager::GetGPUStats() const
{
	FTerraDyneGPUStats Stats;
	
	int32 GPUChunks = 0;
	int32 CPUChunks = 0;
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value && Pair.Value->IsUsingGPU())
		{
			GPUChunks++;
		}
		else
		{
			CPUChunks++;
		}
	}
	
	if (GPUChunks > 0)
	{
		Stats.AdapterName = TEXT("Hybrid Render Target Support");
		Stats.bIsCudaAvailable = true;
		Stats.ComputeBackend = FString::Printf(TEXT("Hybrid CPU Mesh (%d RT-enabled chunks, %d CPU-only)"), GPUChunks, CPUChunks);
	}
	else
	{
		Stats.AdapterName = TEXT("CPU Dynamic Mesh");
		Stats.bIsCudaAvailable = false;
		Stats.ComputeBackend = TEXT("CPU Sculpt Pipeline");
	}
	
	Stats.DedicatedVideoMemory = 0;
	return Stats;
}

int32 ATerraDyneManager::GetTotalVertexCount() const
{
	int32 Total = 0;
	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			int32 Res = Pair.Value->Resolution;
			Total += Res * Res;
		}
	}
	return Total;
}

void ATerraDyneManager::ApplyWorldPreset(UTerraDyneWorldPreset* Preset)
{
	UTerraDyneWorldPreset* PresetToApply = Preset ? Preset : WorldPreset.Get();
	if (!PresetToApply)
	{
		return;
	}

	WorldPreset = PresetToApply;
	ProceduralWorldSettings = PresetToApply->ProceduralSettings;
	BiomeOverlays = PresetToApply->BiomeOverlays;
	PopulationSpawnRules = PresetToApply->PopulationSpawnRules;
	AISpawnZones = PresetToApply->AISpawnZones;
	BuildPermissionZones = PresetToApply->BuildPermissionZones;
	bSpawnDefaultChunksOnBeginPlay = PresetToApply->bSpawnDefaultChunksOnBeginPlay;
	bSetupDefaultLightingOnBeginPlay = PresetToApply->bSetupDefaultLightingOnBeginPlay;
	bRefreshNavMeshOnTerrainChanges = PresetToApply->bRefreshNavMeshOnTerrainChanges;
	bRefreshNavMeshOnPopulationChanges = PresetToApply->bRefreshNavMeshOnPopulationChanges;
	NavigationDirtyBoundsPadding = PresetToApply->NavigationDirtyBoundsPadding;
	OnRep_FrameworkConfig();
}


FName ATerraDyneManager::GetBiomeTagAtLocation(FVector WorldLocation) const
{
	const FIntPoint Coord = WorldToChunkCoord(WorldLocation);
	if (const FTerraDyneProceduralChunkState* ChunkState = FindProceduralChunkState(Coord))
	{
		return ChunkState->PrimaryBiomeTag;
	}

	return ResolveBiomeForChunk(Coord, GetChunkAtCoord(Coord));
}

TArray<FTerraDyneAISpawnZone> ATerraDyneManager::GetAISpawnZonesAtLocation(FVector WorldLocation) const
{
	TArray<FTerraDyneAISpawnZone> MatchingZones;
	const FVector LocalLocation = WorldLocation - GetActorLocation();
	const FIntPoint Coord = WorldToChunkCoord(WorldLocation);
	const bool bProceduralChunk = !ImportedChunkCoords.Contains(Coord);
	const FName BiomeTag = GetBiomeTagAtLocation(WorldLocation);

	for (const FTerraDyneAISpawnZone& Zone : AISpawnZones)
	{
		if (!Zone.bEnabled || !Zone.LocalBounds.IsInsideOrOn(LocalLocation))
		{
			continue;
		}

		if (Zone.bProceduralOnly && !bProceduralChunk)
		{
			continue;
		}

		if (Zone.RequiredBiomeTag != NAME_None && Zone.RequiredBiomeTag != BiomeTag)
		{
			continue;
		}

		MatchingZones.Add(Zone);
	}

	return MatchingZones;
}

float ATerraDyneManager::GetSlopeDegreesAtLocation(FVector WorldLocation) const
{
	const ATerraDyneChunk* Chunk = GetChunkAtLocation(WorldLocation);
	if (!Chunk)
	{
		return 0.0f;
	}

	const float SampleOffset = FMath::Max(100.0f, Chunk->WorldSize / FMath::Max(2, Chunk->Resolution - 1));
	const FVector ChunkLocal = WorldLocation - Chunk->GetActorLocation();
	const float HX0 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X - SampleOffset, ChunkLocal.Y, 0.0f));
	const float HX1 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X + SampleOffset, ChunkLocal.Y, 0.0f));
	const float HY0 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y - SampleOffset, 0.0f));
	const float HY1 = Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y + SampleOffset, 0.0f));
	const float DX = (HX1 - HX0) / (2.0f * SampleOffset);
	const float DY = (HY1 - HY0) / (2.0f * SampleOffset);
	return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
}

bool ATerraDyneManager::CanBuildAtLocation(FVector WorldLocation, FString& OutReason) const
{
	OutReason.Reset();
	const FVector LocalLocation = WorldLocation - GetActorLocation();
	const float SlopeDegrees = GetSlopeDegreesAtLocation(WorldLocation);

	for (const FTerraDyneBuildPermissionZone& Zone : BuildPermissionZones)
	{
		if (!Zone.LocalBounds.IsInsideOrOn(LocalLocation))
		{
			continue;
		}

		if (SlopeDegrees > Zone.MaxSlopeDegrees)
		{
			OutReason = Zone.Reason.IsEmpty()
				? FString::Printf(TEXT("Slope %.1f exceeds %.1f degrees in zone %s."), SlopeDegrees, Zone.MaxSlopeDegrees, *Zone.ZoneId.ToString())
				: Zone.Reason;
			return false;
		}

		if (Zone.Permission == ETerraDyneBuildPermission::Blocked)
		{
			OutReason = Zone.Reason.IsEmpty()
				? FString::Printf(TEXT("Blocked by build zone %s."), *Zone.ZoneId.ToString())
				: Zone.Reason;
			return false;
		}

		if (Zone.Permission == ETerraDyneBuildPermission::Allowed)
		{
			continue;
		}
	}

	return true;
}

TArray<FTerraDynePCGPoint> ATerraDyneManager::GetPCGSeedPointsForChunk(
	FIntPoint Coord,
	bool bIncludePopulation,
	bool bIncludeAISpawnZones) const
{
	TArray<FTerraDynePCGPoint> Points;
	const FName BiomeTag = GetBiomeTagAtLocation(
		GetActorLocation() + FVector(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0.0f));
	const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord);
	const FBox ChunkBounds = Chunk
		? Chunk->GetWorldBounds()
		: TerraDyneWorldFramework::MakeChunkBounds(*this, Coord, GlobalChunkSize);

	if (bIncludePopulation)
	{
		for (const FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
		{
			if (Entry.ChunkCoord != Coord || Entry.State != ETerraDynePopulationState::Active)
			{
				continue;
			}

			FTerraDynePCGPoint Point;
			Point.Transform = TerraDyneWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
			Point.BiomeTag = Entry.CachedBiomeTag != NAME_None ? Entry.CachedBiomeTag : BiomeTag;
			Point.SourceId = Entry.Descriptor.TypeId;
			Point.PopulationKind = Entry.Descriptor.Kind;
			Points.Add(Point);
		}
	}

	if (bIncludeAISpawnZones)
	{
		for (const FTerraDyneAISpawnZone& Zone : AISpawnZones)
		{
			const FBox WorldBounds = Zone.LocalBounds.ShiftBy(GetActorLocation());
			if (!WorldBounds.Intersect(ChunkBounds))
			{
				continue;
			}

			FTerraDynePCGPoint Point;
			Point.Transform = FTransform(WorldBounds.GetCenter());
			Point.BiomeTag = Zone.RequiredBiomeTag != NAME_None ? Zone.RequiredBiomeTag : BiomeTag;
			Point.SourceId = Zone.ZoneId;
			Point.PopulationKind = ETerraDynePopulationKind::ProceduralSpawn;
			Point.Density = Zone.MaxConcurrentSpawns;
			Points.Add(Point);
		}
	}

	return Points;
}

void ATerraDyneManager::BroadcastTerrainChanged(const TArray<FIntPoint>& ChangedCoords)
{
	for (const FIntPoint& Coord : ChangedCoords)
	{
		FTerraDyneTerrainChangeEvent Event;
		Event.ChunkCoord = Coord;
		if (const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord))
		{
			Event.WorldBounds = Chunk->GetWorldBounds();
		}
		else
		{
			Event.WorldBounds = TerraDyneWorldFramework::MakeChunkBounds(*this, Coord, GlobalChunkSize);
		}
		Event.bAffectsNavigation = bRefreshNavMeshOnTerrainChanges;
		OnTerrainChanged.Broadcast(Event);
		RefreshNavigationForBounds(Event.WorldBounds, false);
	}
}

void ATerraDyneManager::BroadcastFoliageChanged(const TArray<FIntPoint>& ChangedCoords)
{
	for (const FIntPoint& Coord : ChangedCoords)
	{
		FTerraDyneFoliageChangeEvent Event;
		Event.ChunkCoord = Coord;
		if (const ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord))
		{
			Event.ImportedFoliageInstanceCount =
				Chunk->GetTransferredFoliageInstanceCount() +
				Chunk->GetTransferredActorFoliageInstanceCount();
		}
		OnFoliageChanged.Broadcast(Event);
	}
}


void ATerraDyneManager::RefreshNavigationForBounds(const FBox& Bounds, bool bPopulationChange)
{
	if (!HasAuthority() || !GetWorld() || !Bounds.IsValid)
	{
		return;
	}

	if ((bPopulationChange && !bRefreshNavMeshOnPopulationChanges) ||
		(!bPopulationChange && !bRefreshNavMeshOnTerrainChanges))
	{
		return;
	}

	if (UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		const FBox ExpandedBounds = Bounds.ExpandBy(FMath::Max(0.0f, NavigationDirtyBoundsPadding));
		NavSystem->AddDirtyArea(ExpandedBounds, ENavigationDirtyFlag::All, FName(TEXT("TerraDyne")));
	}
}

void ATerraDyneManager::SendChunkSculptSync(ATerraDyneChunk* Chunk)
{
	if (!HasAuthority() || !Chunk)
	{
		return;
	}

	// Collect only the entries that actually differ from flat. Untouched ground is zero, so a
	// typical stroke reduces tens of thousands of floats to a few hundred pairs.
	TArray<int32> Indices;
	TArray<float> Values;
	Indices.Reserve(256);
	Values.Reserve(256);
	for (int32 Index = 0; Index < Chunk->SculptBuffer.Num(); Index++)
	{
		const float Value = Chunk->SculptBuffer[Index];
		if (!FMath::IsNearlyZero(Value))
		{
			Indices.Add(Index);
			Values.Add(Value);
		}
	}

	// 8 bytes per pair, so this caps a batch around 24 KB — comfortably inside the 65,536-byte
	// bunch limit even after RPC and array overhead.
	const int32 MaxPairsPerBatch = 3000;
	const int32 NumPairs = Indices.Num();
	const int32 NumBatches = FMath::Max(1, FMath::DivideAndRoundUp(NumPairs, MaxPairsPerBatch));

	for (int32 Batch = 0; Batch < NumBatches; Batch++)
	{
		const int32 Start = Batch * MaxPairsPerBatch;
		const int32 Count = FMath::Min(MaxPairsPerBatch, NumPairs - Start);

		TArray<int32> BatchIndices;
		TArray<float> BatchValues;
		if (Count > 0)
		{
			BatchIndices.Append(Indices.GetData() + Start, Count);
			BatchValues.Append(Values.GetData() + Start, Count);
		}

		Multicast_SyncChunkSculpt(
			Chunk->GridCoordinate,
			BatchIndices,
			BatchValues,
			/*bFirstBatch=*/Batch == 0,
			/*bFinalBatch=*/Batch == NumBatches - 1);
	}
}

void ATerraDyneManager::Multicast_SyncChunkSculpt_Implementation(FIntPoint Coord, const TArray<int32>& Indices, const TArray<float>& Values, bool bFirstBatch, bool bFinalBatch)
{
	// The server already holds the restored state; this only repairs remote copies.
	if (HasAuthority())
	{
		return;
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord);
	if (!Chunk)
	{
		return;
	}

	const int32 ExpectedSize = Chunk->HeightBuffer.Num();

	if (bFirstBatch)
	{
		// Sparse data only describes non-zero entries, so anything previously sculpted and now
		// flat has to be cleared here rather than overwritten.
		Chunk->SculptBuffer.Reset();
		Chunk->SculptBuffer.SetNumZeroed(ExpectedSize);
	}

	const int32 Count = FMath::Min(Indices.Num(), Values.Num());
	for (int32 i = 0; i < Count; i++)
	{
		if (Chunk->SculptBuffer.IsValidIndex(Indices[i]))
		{
			Chunk->SculptBuffer[Indices[i]] = Values[i];
		}
	}

	if (!bFinalBatch)
	{
		return;
	}

	Chunk->RecomputeHeightFromLayers();

	Chunk->RebuildPhysicsMesh();
	Chunk->RequestTransferredFoliageRefresh();

	TArray<FIntPoint> ChangedCoords;
	ChangedCoords.Add(Coord);
	BroadcastTerrainChanged(ChangedCoords);
	BroadcastFoliageChanged(ChangedCoords);
}

void ATerraDyneManager::SendFullSyncToController(ATerraDyneEditController* Controller)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	for (const TPair<FIntPoint, ATerraDyneChunk*>& Pair : ActiveChunkMap)
	{
		if (ATerraDyneChunk* Chunk = Pair.Value)
		{
			SendChunkSculptSync(Chunk);
		}
	}
}

void ATerraDyneManager::Multicast_ApplyBrush_Implementation(const FTerraDyneBrushParams& Params)
{

	// On the server the brush was already applied before the multicast was sent,
	// so only apply on remote clients.
	if (!HasAuthority())
	{
		ApplyGlobalBrush(Params.WorldLocation, Params.Radius, Params.Strength, Params.BrushMode, Params.FlattenHeight);
	}
}

void ATerraDyneManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyTrackedPopulationActors();

	// [C-5] Unregister so the subsystem does not hold a stale pointer after destruction
	if (UWorld* World = GetWorld())
	{
		if (UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>())
		{
			Sys->UnregisterManager(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ---- Streaming: Helpers ----

FIntPoint ATerraDyneManager::WorldToChunkCoord(const FVector& WorldPos) const
{
	const FVector LocalPos = WorldPos - GetActorLocation();
	return FIntPoint(
		TerraDyneWorldFramework::ChunkIndexFromLocalAxis(LocalPos.X, GlobalChunkSize),
		TerraDyneWorldFramework::ChunkIndexFromLocalAxis(LocalPos.Y, GlobalChunkSize)
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

void ATerraDyneManager::MarkChunkDirty(FIntPoint Coord)
{
	DirtyChunkSet.Add(Coord);
}

ATerraDyneChunk* ATerraDyneManager::SpawnConfiguredChunk(FIntPoint Coord, int32 OverrideResolution)
{
	if (!GetWorld())
	{
		return nullptr;
	}

	TSubclassOf<ATerraDyneChunk> ClassToSpawn = ChunkClass;
	if (!ClassToSpawn)
	{
		ClassToSpawn = ATerraDyneChunk::StaticClass();
	}
	const FVector Location = GetActorLocation() + FVector(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0.0f);

	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;

	ATerraDyneChunk* Chunk = GetWorld()->SpawnActor<ATerraDyneChunk>(ClassToSpawn, FTransform(Location), Params);
	if (!Chunk)
	{
		return nullptr;
	}

	Chunk->GridCoordinate = Coord;
	Chunk->ChunkSizeWorldUnits = GlobalChunkSize;
	Chunk->BrushMaterialBase = HeightBrushMaterial;
	Chunk->WorldSize = GlobalChunkSize;
	Chunk->ProceduralSeed = MakeChunkSeed(Coord);
	Chunk->bIsAuthoredChunk = ImportedChunkCoords.Contains(Coord);

	if (OverrideResolution != INDEX_NONE)
	{
		Chunk->Resolution = FMath::Clamp(OverrideResolution, 32, 256);
	}
	else if (const UTerraDyneSettings* S = GetDefault<UTerraDyneSettings>())
	{
		Chunk->Resolution = FMath::Clamp(S->DefaultResolution, 32, 256);
	}
	else
	{
		Chunk->Resolution = 64;
		UE_LOG(LogTemp, Warning, TEXT("TerraDyneSettings CDO not found; using default Resolution=64"));
	}

	// FinishSpawning() synchronously runs BeginPlay(), which (if !bInitialized) calls Initialize()
	// then Manager->RegisterChunk(this) — RegisterChunk() already adds this chunk to
	// ActiveChunkMap and calls SetMaterial(MasterMaterial) using the now-populated height/weight
	// data. Only fall back to doing it here if that didn't happen (e.g. subsystem lookup failed).
	Chunk->FinishSpawning(FTransform(Location));

	if (!ActiveChunkMap.Contains(Coord))
	{
		Chunk->SetMaterial(MasterMaterial);
		ActiveChunkMap.Add(Coord, Chunk);
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	return Chunk;
}

// ---- Streaming: Load/Unload ----

void ATerraDyneManager::LoadOrSpawnChunk(FIntPoint Coord)
{
	if (ActiveChunkMap.Contains(Coord)) return;
	if (!ShouldAllowChunkCoord(Coord)) return;
	ATerraDyneChunk* Chunk = SpawnConfiguredChunk(Coord);
	if (!Chunk) return;

	// Try loading cached data (previously sculpted/painted chunk)
	FTerraDyneChunkData CachedData;
	if (LoadChunkFromCache(Coord, CachedData))
	{
		Chunk->LoadFromData(CachedData);
		// LoadFromData re-initializes the chunk when the cached resolution differs, which resizes
		// the pad buffers back to zero. Re-stamp after the load rather than before it.
		RefreshBuildingPadsForChunk(Chunk);
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Loaded chunk [%d,%d] from cache"), Coord.X, Coord.Y);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Spawned fresh chunk [%d,%d]"), Coord.X, Coord.Y);
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	SyncPopulationForChunk(Coord);
	PendingLoadQueue.Remove(Coord);
}

void ATerraDyneManager::UnloadChunk(FIntPoint Coord)
{
	ATerraDyneChunk** Found = ActiveChunkMap.Find(Coord);
	if (!Found || !(*Found)) return;

	ATerraDyneChunk* Chunk = *Found;
	DespawnPopulationForChunk(Coord);

	// Persist dirty chunks
	if (DirtyChunkSet.Contains(Coord))
	{
		FTerraDyneChunkData Data = Chunk->GetSerializedData();
		SaveChunkToCache(Coord, Data);
		DirtyChunkSet.Remove(Coord);
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Saved dirty chunk [%d,%d] to cache"), Coord.X, Coord.Y);
	}

	ActiveChunkMap.Remove(Coord);
	Chunk->Destroy();
	PendingUnloadQueue.Remove(Coord);

	UE_LOG(LogTemp, Verbose, TEXT("Streaming: Unloaded chunk [%d,%d]"), Coord.X, Coord.Y);
}

// ---- Streaming: Ring Update ----

void ATerraDyneManager::UpdateStreaming(const TArray<FVector>& PlayerPositions)
{
	if (bStreamingPaused || PlayerPositions.Num() == 0) return;

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	if (!Settings) return;

	const int32 LoadRadius = Settings->ChunkLoadRadius;
	const int32 UnloadRadius = Settings->ChunkUnloadRadius;

	// Compute a hash of all player chunk coordinates to detect movement
	uint32 CurrentHash = 0;
	TArray<FIntPoint> PlayerChunks;
	for (const FVector& Pos : PlayerPositions)
	{
		FIntPoint PC = WorldToChunkCoord(Pos);
		PlayerChunks.Add(PC);
		CurrentHash = HashCombine(CurrentHash, GetTypeHash(PC));
	}

	// Only rescan when any player moves cells
	if (CurrentHash != LastStreamingHash)
	{
		LastStreamingHash = CurrentHash;

		// Build union of load regions across all players
		for (const FIntPoint& PlayerChunk : PlayerChunks)
		{
			for (int32 DX = -LoadRadius; DX <= LoadRadius; DX++)
			{
				for (int32 DY = -LoadRadius; DY <= LoadRadius; DY++)
				{
					// Diamond shape: Manhattan distance
					if (FMath::Abs(DX) + FMath::Abs(DY) > LoadRadius) continue;

					FIntPoint Coord(PlayerChunk.X + DX, PlayerChunk.Y + DY);
					if (!ShouldAllowChunkCoord(Coord))
					{
						continue;
					}

					if (!ActiveChunkMap.Contains(Coord) && !PendingLoadQueue.Contains(Coord))
					{
						PendingLoadQueue.Add(Coord);
					}
				}
			}
		}

		// Queue chunks outside ALL players' unload radius
		TArray<FIntPoint> ToUnload;
		for (const auto& Pair : ActiveChunkMap)
		{
			bool bInAnyPlayerRange = false;
			for (const FIntPoint& PlayerChunk : PlayerChunks)
			{
				int32 Dist = FMath::Abs(Pair.Key.X - PlayerChunk.X) + FMath::Abs(Pair.Key.Y - PlayerChunk.Y);
				if (Dist <= UnloadRadius)
				{
					bInAnyPlayerRange = true;
					break;
				}
			}
			if (!bInAnyPlayerRange && !PendingUnloadQueue.Contains(Pair.Key))
			{
				ToUnload.Add(Pair.Key);
			}
		}
		for (const FIntPoint& Coord : ToUnload)
		{
			PendingUnloadQueue.Add(Coord);
		}
	}

	ProcessStreamingQueues(PlayerPositions);
}

void ATerraDyneManager::ProcessStreamingQueues(const TArray<FVector>& PlayerPositions)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxOps = Settings ? Settings->MaxChunkOpsPerTick : 2;
	int32 Ops = 0;

	// Pre-compute player chunk coords for sorting
	TArray<FIntPoint> PlayerChunks;
	for (const FVector& Pos : PlayerPositions)
	{
		PlayerChunks.Add(WorldToChunkCoord(Pos));
	}

	// Helper: minimum Manhattan distance from a chunk to any player
	auto MinDistToAnyPlayer = [&PlayerChunks](const FIntPoint& Coord) -> int32
	{
		int32 MinDist = TNumericLimits<int32>::Max();
		for (const FIntPoint& PC : PlayerChunks)
		{
			int32 D = FMath::Abs(Coord.X - PC.X) + FMath::Abs(Coord.Y - PC.Y);
			MinDist = FMath::Min(MinDist, D);
		}
		return MinDist;
	};

	// Unloads first (free memory before allocating) — farthest from any player first
	if (PendingUnloadQueue.Num() > 0 && Ops < MaxOps)
	{
		TArray<FIntPoint> UnloadList = PendingUnloadQueue.Array();
		UnloadList.Sort([&MinDistToAnyPlayer](const FIntPoint& A, const FIntPoint& B)
		{
			return MinDistToAnyPlayer(A) > MinDistToAnyPlayer(B);
		});

		for (const FIntPoint& Coord : UnloadList)
		{
			if (Ops >= MaxOps) break;
			UnloadChunk(Coord);
			Ops++;
		}
	}

	// Loads next — nearest to any player first
	if (PendingLoadQueue.Num() > 0 && Ops < MaxOps)
	{
		TArray<FIntPoint> LoadList = PendingLoadQueue.Array();
		LoadList.Sort([&MinDistToAnyPlayer](const FIntPoint& A, const FIntPoint& B)
		{
			return MinDistToAnyPlayer(A) < MinDistToAnyPlayer(B);
		});

		for (const FIntPoint& Coord : LoadList)
		{
			if (Ops >= MaxOps) break;
			LoadOrSpawnChunk(Coord);
			Ops++;
		}
	}
}

