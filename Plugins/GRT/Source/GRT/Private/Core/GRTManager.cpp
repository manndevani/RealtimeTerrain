// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/GRTManager.h"
#include "Core/GRTSubsystem.h"
#include "Core/GRTEditController.h"
#include "Core/GRTSaveGame.h"
#include "Core/GRTWorldPreset.h"
#include "Settings/GRTSettings.h"
#include "World/GRTChunk.h"
#include "World/GRTOrchestrator.h"
#include "Building/GRTBuildingActor.h"
#include "GRTModule.h"
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
#include "IO/GRTGeoTIFFHeightSource.h"


#include "Core/GRTManagerInternal.h"

AGRTManager::AGRTManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	GlobalChunkSize = 10000.0f;
	ChunkClass = AGRTChunk::StaticClass();
	bAutoImportAtRuntime = false;
	bSpawnDefaultChunksOnBeginPlay = false;

	// One slot per layer, always addressable so the UI can show every layer's state whether or not
	// anything has been imported into it.
	RasterLayerSlots.SetNum((int32)EGRTRasterLayer::MAX);
	bSetupDefaultLightingOnBeginPlay = false;
	bSpawnShowcaseOnBeginPlay = false;
	PopulationMaintenanceTimer = 0.25f;
}

AGRTManager::~AGRTManager() = default;

AGRTManager::AGRTManager(FVTableHelper& Helper) : Super(Helper)
{
}

void AGRTManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGRTManager, GlobalChunkSize);
	DOREPLIFETIME(AGRTManager, ChunkClass);
	DOREPLIFETIME(AGRTManager, ActiveLayer);
	DOREPLIFETIME(AGRTManager, MasterMaterial);
	DOREPLIFETIME(AGRTManager, LandscapeMigrationState);
	DOREPLIFETIME(AGRTManager, ProceduralWorldSettings);
	DOREPLIFETIME(AGRTManager, BiomeOverlays);
	DOREPLIFETIME(AGRTManager, AISpawnZones);
	DOREPLIFETIME(AGRTManager, BuildPermissionZones);
	DOREPLIFETIME(AGRTManager, GeoTIFFImportState);
	DOREPLIFETIME(AGRTManager, bAwaitingWorldModeChoice);
	DOREPLIFETIME(AGRTManager, WorldOriginKind);
	DOREPLIFETIME(AGRTManager, RasterLayerSlots);
	DOREPLIFETIME(AGRTManager, DynamicRasterLayers);
}

void AGRTManager::OnRep_RasterLayerSlots()
{
	UE_LOG(LogGRT, Verbose, TEXT("GRT: Replicated raster layer slots updated on client."));
}

void AGRTManager::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("GRTManager: Starting initialization..."));

	PurgeStaleChunkCacheSessions();

	// Register with subsystem
	if (UGRTSubsystem* Subsystem = GetWorld()->GetSubsystem<UGRTSubsystem>())
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
		if (WorldMode == EGRTWorldMode::Imported && GeoTIFFImportState.bWasImportedFromGeoTIFF && !ActiveGeoTIFFHeightSource.IsValid())
		{
			// Resuming an already-decided world -- the level itself already carries GeoTIFFImportState
			// (a normal saved UPROPERTY on this actor) from an earlier import, but ActiveGeoTIFFHeightSource
			// is not saved -- so re-decode from the same source, regardless of
			// bShowWorldModeChoiceOnBeginPlay, so chunks that haven't been visited yet still resolve
			// correct DEM data. This is never a fresh choice, so it is not gated by it. Checked before
			// GeoTIFFImportPath below since a previously-imported level's saved state carries both.
			// Don't clear/re-georeference: RebuildChunkMap() above already captured whatever chunks the
			// level itself saved, and the origin was already established last time.
			FGRTGeoTIFFImportOptions ReloadOptions = GeoTIFFImportOptions;
			ReloadOptions.bClearExistingChunks = false;
			ReloadOptions.bEstablishGeoreferenceOrigin = false;
			ImportGeoTIFFInternal(GeoTIFFImportState.SourceFilePath, ReloadOptions);
			bAwaitingWorldModeChoice = false;
		}
		else if (!bShowWorldModeChoiceOnBeginPlay && WorldMode == EGRTWorldMode::Imported && !GeoTIFFImportPath.IsEmpty())
		{
			// A level/Blueprint explicitly pre-configured both WorldMode and a fixed GeoTIFFImportPath
			// at design time, and turned the interactive choice off -- so there is nothing left for a
			// player to decide. ImportGeoTIFF() already rebuilds the chunk map / reapplies materials /
			// refreshes population itself (it has to -- it's also the entry point the runtime
			// file-picker calls long after BeginPlay), so this doesn't need bChunkMapDirty too.
			ImportGeoTIFF(GeoTIFFImportPath);
			bAwaitingWorldModeChoice = false;
		}
		else if (!bShowWorldModeChoiceOnBeginPlay && WorldMode == EGRTWorldMode::Sandbox && bSpawnDefaultChunksOnBeginPlay && ActiveChunkMap.Num() == 0)
		{
			SpawnDefaultSandboxChunk();
			bChunkMapDirty = true;
			bAwaitingWorldModeChoice = false;
		}
		else if (!bShowWorldModeChoiceOnBeginPlay)
		{
			// Nothing above matched but the level still opted out of the choice screen entirely (e.g.
			// bSpawnDefaultChunksOnBeginPlay is also off) -- still not "awaiting" anything, since
			// there is no choice screen for the player to be awaiting in the first place.
			bAwaitingWorldModeChoice = false;
		}
		// else: bShowWorldModeChoiceOnBeginPlay is true (the default) and nothing has been decided
		// yet. bAwaitingWorldModeChoice defaults true at construction (see its own doc comment for
		// why -- other actors' BeginPlay order relative to this one is not guaranteed), so this
		// branch deliberately does nothing: the default already holds. BeginPlay itself spawns
		// nothing either way; UpdateStreaming (Tick-driven) is what this flag actually guards,
		// staying off until StartSandboxMode() or a successful ImportGeoTIFF() clears it.

		if (bSetupDefaultLightingOnBeginPlay)
		{
			SetupLighting();
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
	
	UE_LOG(LogTemp, Log, TEXT("GRTManager: Initialization complete"));
}

void AGRTManager::Tick(float DeltaTime)
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

void AGRTManager::LoadMaterials()
{
	if (bMaterialsLoaded) return;

	UE_LOG(LogTemp, Log, TEXT("Loading GRT materials..."));

	// [N-2] Guard: CDO lookup can return nullptr during hot-reload edge cases
	const UGRTSettings* Settings = GetDefault<UGRTSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("GRTSettings CDO not found"));
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

void AGRTManager::SetupLighting()
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

void AGRTManager::StartSandboxMode()
{
	WorldOriginKind = EGRTChunkOrigin::Procedural;

	if (ActiveChunkMap.Num() == 0)
	{
		MarkWorldModeChosen();
		if (HasAuthority())
		{
			SpawnDefaultSandboxChunk();
			RebuildChunkMap();
			ApplyMaterialToActiveChunks();
			RefreshPopulationForLoadedChunks();
		}
	}
	else
	{
		MarkWorldModeChosen();
	}
}

void AGRTManager::StartDEMImportMode()
{
	WorldOriginKind = EGRTChunkOrigin::ImportedGeoTIFF;

	PendingLoadQueue.Reset();
	PendingUnloadQueue.Reset();
	DirtyChunkSet.Reset();
	ResetChunkCacheSession();

	// Destroy all existing chunk actors so DEM session is completely empty
	if (UWorld* World = GetWorld())
	{
		TArray<AActor*> AllChunks;
		UGameplayStatics::GetAllActorsOfClass(World, AGRTChunk::StaticClass(), AllChunks);
		for (AActor* ChunkActor : AllChunks)
		{
			if (ChunkActor)
			{
				ChunkActor->Destroy();
			}
		}
	}
	ActiveChunkMap.Reset();
	if (GetWorld())
	{
		RebuildChunkMap();
	}

	MarkWorldModeChosen();
}

void AGRTManager::MarkWorldModeChosen()
{
	if (!bAwaitingWorldModeChoice)
	{
		return;
	}

	bAwaitingWorldModeChoice = false;

	// Streaming only rescans when the player's chunk-coordinate hash changes. On the late path --
	// a mode chosen after streaming has already run once -- that hash is already cached, and the
	// player standing still would leave the world empty until they happened to walk into a new
	// chunk. Zeroing it forces one rescan, the same idiom the Landscape-import and save-load paths
	// already use for the same reason.
	LastStreamingHash = 0;

	// Broadcast after the flag is actually cleared, so anything bound here (the showcase sequence,
	// notably) that turns around and inspects bAwaitingWorldModeChoice sees the resolved state.
	OnWorldModeChosen.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("GRT: world mode chosen -- chunk streaming released."));
}

void AGRTManager::SpawnDefaultSandboxChunk()
{
	UE_LOG(LogTemp, Log, TEXT("Spawning 3x3 chunk grid..."));
	
	for (int32 X = -1; X <= 1; X++)
	{
		for (int32 Y = -1; Y <= 1; Y++)
		{
			FIntPoint GridCoord(X, Y);

			AGRTChunk* Chunk = SpawnConfiguredChunk(GridCoord);
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

void AGRTManager::ApplyGlobalBrush(FVector WorldLocation, float Radius, float Strength,
	EGRTBrushMode BrushMode, float FlattenHeight)
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
	for (AGRTChunk* Chunk : GetChunksInBoxXY(BrushBounds))
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

AGRTChunk* AGRTManager::GetChunkAtLocation(FVector WorldLocation) const
{
	if (ActiveChunkMap.Num() == 0)
	{
		const_cast<AGRTManager*>(this)->RebuildChunkMap();
	}
	if (AGRTChunk* const* Chunk = ActiveChunkMap.Find(WorldToChunkCoord(WorldLocation))) return *Chunk;

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

FBox2D AGRTManager::GetChunkBoundsXY(const AGRTChunk* Chunk) const
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

TArray<AGRTChunk*> AGRTManager::GetChunksInBoxXY(const FBox2D& WorldAreaXY) const
{
	TArray<AGRTChunk*> Result;
	if (!WorldAreaXY.bIsValid)
	{
		return Result;
	}

	if (ActiveChunkMap.Num() == 0)
	{
		const_cast<AGRTManager*>(this)->RebuildChunkMap();
	}

	for (const TPair<FIntPoint, AGRTChunk*>& Pair : ActiveChunkMap)
	{
		AGRTChunk* Chunk = Pair.Value;
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

bool AGRTManager::GetNaturalTerrainHeightAtXY(FVector2D WorldXY, float& OutWorldZ) const
{
	const FVector Probe(WorldXY.X, WorldXY.Y, 0.0f);
	const AGRTChunk* Chunk = GetChunkAtLocation(Probe);
	if (!Chunk)
	{
		return false;
	}

	const FVector ChunkOrigin = Chunk->GetActorLocation();
	OutWorldZ = ChunkOrigin.Z + Chunk->GetNaturalHeightAtLocation(Probe - ChunkOrigin);
	return true;
}

void AGRTManager::RegisterBuilding(AGRTBuildingActor* Building)
{
	if (!Building)
	{
		return;
	}

	RegisteredBuildings.RemoveAll([](const TWeakObjectPtr<AGRTBuildingActor>& Entry)
	{
		return !Entry.IsValid();
	});

	RegisteredBuildings.AddUnique(Building);
}

void AGRTManager::UnregisterBuilding(AGRTBuildingActor* Building)
{
	if (!Building)
	{
		return;
	}

	// Recompute the ground it was standing on before dropping it, otherwise its pad stays stamped
	// into every chunk it overlapped for as long as those chunks remain loaded.
	const FBox2D Area = Building->GetFootprintBoundsWorld(Building->PadFalloff);

	RegisteredBuildings.RemoveAll([Building](const TWeakObjectPtr<AGRTBuildingActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Building;
	});

	RefreshBuildingPads(Area);
}

void AGRTManager::RefreshBuildingPads(const FBox2D& WorldAreaXY)
{
	if (!WorldAreaXY.bIsValid)
	{
		return;
	}

	TArray<FIntPoint> ChangedCoords;
	for (AGRTChunk* Chunk : GetChunksInBoxXY(WorldAreaXY))
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

void AGRTManager::Multicast_RefreshBuildingPads_Implementation(FVector2D MinXY, FVector2D MaxXY)
{
	if (!HasAuthority())
	{
		RefreshBuildingPads(FBox2D(MinXY, MaxXY));
	}
}

void AGRTManager::SetExternalPadStamps(AActor* PadOwner, const TArray<FGRTPadStamp>& Stamps)
{
	if (!PadOwner)
	{
		return;
	}

	// Drop dead owners while we are here, so nothing leaks when an actor is destroyed without
	// clearing its stamps first.
	ExternalPadEntries.RemoveAll([PadOwner](const FExternalPadEntry& Entry)
	{
		return !Entry.Owner.IsValid() || Entry.Owner.Get() == PadOwner;
	});

	if (Stamps.Num() > 0)
	{
		FExternalPadEntry Entry;
		Entry.Owner = PadOwner;
		Entry.Stamps = Stamps;
		ExternalPadEntries.Add(MoveTemp(Entry));
	}
}

void AGRTManager::ClearExternalPadStamps(AActor* PadOwner)
{
	ExternalPadEntries.RemoveAll([PadOwner](const FExternalPadEntry& Entry)
	{
		return !Entry.Owner.IsValid() || Entry.Owner.Get() == PadOwner;
	});
}

FBox2D AGRTManager::GetExternalPadBounds(AActor* PadOwner) const
{
	FBox2D Bounds(ForceInit);

	for (const FExternalPadEntry& Entry : ExternalPadEntries)
	{
		if (Entry.Owner.Get() != PadOwner)
		{
			continue;
		}
		for (const FGRTPadStamp& Stamp : Entry.Stamps)
		{
			for (const FVector2D& Point : Stamp.PolygonWorldXY)
			{
				Bounds += Point;
			}
		}
	}

	if (Bounds.bIsValid)
	{
		// Room for the skirt, which reaches past the footprint itself.
		Bounds = Bounds.ExpandBy(500.0f);
	}
	return Bounds;
}

void AGRTManager::RefreshBuildingPadsForChunk(AGRTChunk* Chunk)
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
	TArray<AGRTBuildingActor*> Overlapping;
	for (const TWeakObjectPtr<AGRTBuildingActor>& Entry : RegisteredBuildings)
	{
		AGRTBuildingActor* Building = Entry.Get();
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

	// Non-building contributors reaching this chunk, gathered the same way.
	TArray<const FGRTPadStamp*> OverlappingStamps;
	for (const FExternalPadEntry& Entry : ExternalPadEntries)
	{
		if (!Entry.Owner.IsValid())
		{
			continue;
		}
		for (const FGRTPadStamp& Stamp : Entry.Stamps)
		{
			if (Stamp.PolygonWorldXY.Num() < 3)
			{
				continue;
			}

			FBox2D StampBox(ForceInit);
			for (const FVector2D& Point : Stamp.PolygonWorldXY)
			{
				StampBox += Point;
			}
			StampBox = StampBox.ExpandBy(FMath::Max(Stamp.Falloff, 1.0f));

			if (StampBox.Intersect(ChunkBox))
			{
				OverlappingStamps.Add(&Stamp);
			}
		}
	}

	const bool bHadPad = Chunk->HasBuildingPad();
	if (Overlapping.Num() == 0 && OverlappingStamps.Num() == 0 && !bHadPad)
	{
		return;
	}

	Chunk->ClearBuildingPad();

	const FVector ChunkOrigin = Chunk->GetActorLocation();
	for (AGRTBuildingActor* Building : Overlapping)
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

	for (const FGRTPadStamp* Stamp : OverlappingStamps)
	{
		TArray<FVector2D> LocalPolygon;
		LocalPolygon.Reserve(Stamp->PolygonWorldXY.Num());
		for (const FVector2D& P : Stamp->PolygonWorldXY)
		{
			LocalPolygon.Emplace(P.X - ChunkOrigin.X, P.Y - ChunkOrigin.Y);
		}

		Chunk->StampBuildingPad(LocalPolygon, Stamp->TargetWorldZ - ChunkOrigin.Z, Stamp->Falloff);
	}

	Chunk->RecomputeHeightFromLayers();
	Chunk->NotifyHeightBufferChanged();
	Chunk->MarkLayerWeightsDirty();
}

TArray<AGRTChunk*> AGRTManager::GetChunksInRadius(FVector WorldLocation, float Radius)
{
	TArray<AGRTChunk*> Result;
	const float RadiusWorld = Radius + GlobalChunkSize * 0.5f;
	const float ProbeInset = 0.01f;
	const FIntPoint MinCoord = WorldToChunkCoord(WorldLocation - FVector(RadiusWorld, RadiusWorld, 0.0f));
	const FIntPoint MaxCoord = WorldToChunkCoord(
		WorldLocation + FVector(RadiusWorld - ProbeInset, RadiusWorld - ProbeInset, 0.0f));
	for (int32 X = MinCoord.X; X <= MaxCoord.X; X++)
	{
		for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; Y++)
		{
			if (AGRTChunk** Found = ActiveChunkMap.Find(FIntPoint(X, Y)))
			{
				if (*Found) Result.Add(*Found);
			}
		}
	}
	return Result;
}

AGRTChunk* AGRTManager::GetChunkAtCoord(FIntPoint Coord) const
{
	AGRTChunk* const* Found = ActiveChunkMap.Find(Coord);
	return Found ? *Found : nullptr;
}

void AGRTManager::RegisterChunk(AGRTChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	if (AGRTChunk** ExistingChunk = ActiveChunkMap.Find(Chunk->GridCoordinate))
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
	if (WorldOriginKind != EGRTChunkOrigin::ImportedGeoTIFF)
	{
		Chunk->SetMaterial(MasterMaterial);
	}
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

void AGRTManager::UnregisterChunk(AGRTChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	if (AGRTChunk** Found = ActiveChunkMap.Find(Chunk->GridCoordinate))
	{
		if (*Found == Chunk)
		{
			ActiveChunkMap.Remove(Chunk->GridCoordinate);
		}
	}
}


void AGRTManager::ApplyGlobalNoise(float Strength, float Frequency, float Seed)
{
	const float SafeFrequency = (Frequency > KINDA_SMALL_NUMBER) ? Frequency : 0.001f;
	TArray<FIntPoint> ChangedCoords;

	for (auto& Pair : ActiveChunkMap)
	{
		AGRTChunk* Chunk = Pair.Value;
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

void AGRTManager::Multicast_ApplyGlobalNoise_Implementation(float Strength, float Frequency, float Seed)
{
	if (!HasAuthority())
	{
		ApplyGlobalNoise(Strength, Frequency, Seed);
	}
}


void AGRTManager::SetAuthoredChunkCoordinates(
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

void AGRTManager::RebuildChunkMap()
{
	ActiveChunkMap.Reset();
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGRTChunk::StaticClass(), FoundActors);
	for (AActor* Actor : FoundActors)
	{
		if (AGRTChunk* Chunk = Cast<AGRTChunk>(Actor))
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

void AGRTManager::ApplyMaterialToActiveChunks()
{
	if (WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF)
	{
		return;
	}

	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			Pair.Value->SetMaterial(MasterMaterial);
		}
	}
}



bool AGRTManager::IsImportedChunkCoord(FIntPoint Coord) const
{
	return ImportedChunkCoords.Contains(Coord);
}

bool AGRTManager::ShouldAllowChunkCoord(FIntPoint Coord) const
{
	if (WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF)
	{
		return ImportedChunkCoords.Contains(Coord) && ActiveGeoTIFFHeightSource.IsValid();
	}

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

		if (const UGRTSettings* Settings = GetDefault<UGRTSettings>())
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

int32 AGRTManager::GetDistanceFromAuthoredChunks(FIntPoint Coord) const
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

int32 AGRTManager::MakeChunkSeed(FIntPoint Coord) const
{
	return static_cast<int32>(HashCombine(GetTypeHash(Coord), GetTypeHash(ProceduralWorldSettings.WorldSeed)));
}

void AGRTManager::RebuildWorldStateIndices()
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

FGRTProceduralChunkState* AGRTManager::FindProceduralChunkState(FIntPoint Coord)
{
	if (const int32* Index = ProceduralChunkStateIndexByCoord.Find(Coord))
	{
		return ProceduralChunkStates.IsValidIndex(*Index) ? &ProceduralChunkStates[*Index] : nullptr;
	}

	return nullptr;
}

const FGRTProceduralChunkState* AGRTManager::FindProceduralChunkState(FIntPoint Coord) const
{
	if (const int32* Index = ProceduralChunkStateIndexByCoord.Find(Coord))
	{
		return ProceduralChunkStates.IsValidIndex(*Index) ? &ProceduralChunkStates[*Index] : nullptr;
	}

	return nullptr;
}

FName AGRTManager::ResolveBiomeForChunk(FIntPoint Coord, const AGRTChunk* Chunk, TArray<FName>* OutOverlays) const
{
	FName SelectedBiome = ImportedChunkCoords.Contains(Coord) ? FName(TEXT("Authored")) : FName(TEXT("Outskirts"));
	int32 SelectedPriority = TNumericLimits<int32>::Lowest();
	const bool bAuthoredChunk = ImportedChunkCoords.Contains(Coord);
	const float ProceduralNoise = FRandomStream(MakeChunkSeed(Coord)).FRand();

	if (OutOverlays)
	{
		OutOverlays->Reset();
	}

	for (const FGRTBiomeOverlay& Overlay : BiomeOverlays)
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

void AGRTManager::EnsureChunkFrameworkState(FIntPoint Coord, AGRTChunk* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	FGRTProceduralChunkState* State = FindProceduralChunkState(Coord);
	if (!State)
	{
		FGRTProceduralChunkState NewState;
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

void AGRTManager::SetMasterMaterial(UMaterialInterface* NewMaterial)
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

void AGRTManager::OnRep_MasterMaterial()
{
	ApplyMaterialToActiveChunks();
}

void AGRTManager::OnRep_LandscapeMigrationState()
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

void AGRTManager::OnRep_FrameworkConfig()
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

FGRTGPUStats AGRTManager::GetGPUStats() const
{
	FGRTGPUStats Stats;
	
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

int32 AGRTManager::GetTotalVertexCount() const
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

void AGRTManager::ApplyWorldPreset(UGRTWorldPreset* Preset)
{
	UGRTWorldPreset* PresetToApply = Preset ? Preset : WorldPreset.Get();
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


FName AGRTManager::GetBiomeTagAtLocation(FVector WorldLocation) const
{
	const FIntPoint Coord = WorldToChunkCoord(WorldLocation);
	if (const FGRTProceduralChunkState* ChunkState = FindProceduralChunkState(Coord))
	{
		return ChunkState->PrimaryBiomeTag;
	}

	return ResolveBiomeForChunk(Coord, GetChunkAtCoord(Coord));
}

TArray<FGRTAISpawnZone> AGRTManager::GetAISpawnZonesAtLocation(FVector WorldLocation) const
{
	TArray<FGRTAISpawnZone> MatchingZones;
	const FVector LocalLocation = WorldLocation - GetActorLocation();
	const FIntPoint Coord = WorldToChunkCoord(WorldLocation);
	const bool bProceduralChunk = !ImportedChunkCoords.Contains(Coord);
	const FName BiomeTag = GetBiomeTagAtLocation(WorldLocation);

	for (const FGRTAISpawnZone& Zone : AISpawnZones)
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

float AGRTManager::GetSlopeDegreesAtLocation(FVector WorldLocation) const
{
	const AGRTChunk* Chunk = GetChunkAtLocation(WorldLocation);
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

bool AGRTManager::CanBuildAtLocation(FVector WorldLocation, FString& OutReason) const
{
	OutReason.Reset();
	const FVector LocalLocation = WorldLocation - GetActorLocation();
	const float SlopeDegrees = GetSlopeDegreesAtLocation(WorldLocation);

	for (const FGRTBuildPermissionZone& Zone : BuildPermissionZones)
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

		if (Zone.Permission == EGRTBuildPermission::Blocked)
		{
			OutReason = Zone.Reason.IsEmpty()
				? FString::Printf(TEXT("Blocked by build zone %s."), *Zone.ZoneId.ToString())
				: Zone.Reason;
			return false;
		}

		if (Zone.Permission == EGRTBuildPermission::Allowed)
		{
			continue;
		}
	}

	return true;
}

TArray<FGRTPCGPoint> AGRTManager::GetPCGSeedPointsForChunk(
	FIntPoint Coord,
	bool bIncludePopulation,
	bool bIncludeAISpawnZones) const
{
	TArray<FGRTPCGPoint> Points;
	const FName BiomeTag = GetBiomeTagAtLocation(
		GetActorLocation() + FVector(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0.0f));
	const AGRTChunk* Chunk = GetChunkAtCoord(Coord);
	const FBox ChunkBounds = Chunk
		? Chunk->GetWorldBounds()
		: GRTWorldFramework::MakeChunkBounds(*this, Coord, GlobalChunkSize);

	if (bIncludePopulation)
	{
		for (const FGRTPersistentPopulationEntry& Entry : PersistentPopulationEntries)
		{
			if (Entry.ChunkCoord != Coord || Entry.State != EGRTPopulationState::Active)
			{
				continue;
			}

			FGRTPCGPoint Point;
			Point.Transform = GRTWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
			Point.BiomeTag = Entry.CachedBiomeTag != NAME_None ? Entry.CachedBiomeTag : BiomeTag;
			Point.SourceId = Entry.Descriptor.TypeId;
			Point.PopulationKind = Entry.Descriptor.Kind;
			Points.Add(Point);
		}
	}

	if (bIncludeAISpawnZones)
	{
		for (const FGRTAISpawnZone& Zone : AISpawnZones)
		{
			const FBox WorldBounds = Zone.LocalBounds.ShiftBy(GetActorLocation());
			if (!WorldBounds.Intersect(ChunkBounds))
			{
				continue;
			}

			FGRTPCGPoint Point;
			Point.Transform = FTransform(WorldBounds.GetCenter());
			Point.BiomeTag = Zone.RequiredBiomeTag != NAME_None ? Zone.RequiredBiomeTag : BiomeTag;
			Point.SourceId = Zone.ZoneId;
			Point.PopulationKind = EGRTPopulationKind::ProceduralSpawn;
			Point.Density = Zone.MaxConcurrentSpawns;
			Points.Add(Point);
		}
	}

	return Points;
}

void AGRTManager::BroadcastTerrainChanged(const TArray<FIntPoint>& ChangedCoords)
{
	for (const FIntPoint& Coord : ChangedCoords)
	{
		FGRTTerrainChangeEvent Event;
		Event.ChunkCoord = Coord;
		if (const AGRTChunk* Chunk = GetChunkAtCoord(Coord))
		{
			Event.WorldBounds = Chunk->GetWorldBounds();
		}
		else
		{
			Event.WorldBounds = GRTWorldFramework::MakeChunkBounds(*this, Coord, GlobalChunkSize);
		}
		Event.bAffectsNavigation = bRefreshNavMeshOnTerrainChanges;
		OnTerrainChanged.Broadcast(Event);
		RefreshNavigationForBounds(Event.WorldBounds, false);
	}
}

void AGRTManager::BroadcastFoliageChanged(const TArray<FIntPoint>& ChangedCoords)
{
	for (const FIntPoint& Coord : ChangedCoords)
	{
		FGRTFoliageChangeEvent Event;
		Event.ChunkCoord = Coord;
		if (const AGRTChunk* Chunk = GetChunkAtCoord(Coord))
		{
			Event.ImportedFoliageInstanceCount =
				Chunk->GetTransferredFoliageInstanceCount() +
				Chunk->GetTransferredActorFoliageInstanceCount();
		}
		OnFoliageChanged.Broadcast(Event);
	}
}


void AGRTManager::RefreshNavigationForBounds(const FBox& Bounds, bool bPopulationChange)
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
		NavSystem->AddDirtyArea(ExpandedBounds, ENavigationDirtyFlag::All, FName(TEXT("GRT")));
	}
}

void AGRTManager::SendChunkSculptSync(AGRTChunk* Chunk)
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

void AGRTManager::Multicast_SyncChunkSculpt_Implementation(FIntPoint Coord, const TArray<int32>& Indices, const TArray<float>& Values, bool bFirstBatch, bool bFinalBatch)
{
	// The server already holds the restored state; this only repairs remote copies.
	if (HasAuthority())
	{
		return;
	}

	AGRTChunk* Chunk = GetChunkAtCoord(Coord);
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

void AGRTManager::SendFullSyncToController(AGRTEditController* Controller)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	for (const TPair<FIntPoint, AGRTChunk*>& Pair : ActiveChunkMap)
	{
		if (AGRTChunk* Chunk = Pair.Value)
		{
			SendChunkSculptSync(Chunk);
		}
	}
}

void AGRTManager::Multicast_ApplyBrush_Implementation(const FGRTBrushParams& Params)
{

	// On the server the brush was already applied before the multicast was sent,
	// so only apply on remote clients.
	if (!HasAuthority())
	{
		ApplyGlobalBrush(Params.WorldLocation, Params.Radius, Params.Strength, Params.BrushMode, Params.FlattenHeight);
	}
}

void AGRTManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyTrackedPopulationActors();

	// The eviction cache only lives as long as the session; anything worth keeping is in a save.
	ResetChunkCacheSession();

	// [C-5] Unregister so the subsystem does not hold a stale pointer after destruction
	if (UWorld* World = GetWorld())
	{
		if (UGRTSubsystem* Sys = World->GetSubsystem<UGRTSubsystem>())
		{
			Sys->UnregisterManager(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ---- Streaming: Helpers ----

FIntPoint AGRTManager::WorldToChunkCoord(const FVector& WorldPos) const
{
	const FVector LocalPos = WorldPos - GetActorLocation();
	return FIntPoint(
		GRTWorldFramework::ChunkIndexFromLocalAxis(LocalPos.X, GlobalChunkSize),
		GRTWorldFramework::ChunkIndexFromLocalAxis(LocalPos.Y, GlobalChunkSize)
	);
}


void AGRTManager::MarkChunkDirty(FIntPoint Coord)
{
	DirtyChunkSet.Add(Coord);
}

AGRTChunk* AGRTManager::SpawnConfiguredChunk(FIntPoint Coord, int32 OverrideResolution)
{
	if (!GetWorld())
	{
		return nullptr;
	}

	TSubclassOf<AGRTChunk> ClassToSpawn = ChunkClass;
	if (!ClassToSpawn)
	{
		ClassToSpawn = AGRTChunk::StaticClass();
	}
	const FVector Location = GetActorLocation() + FVector(Coord.X * GlobalChunkSize, Coord.Y * GlobalChunkSize, 0.0f);

	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;

	AGRTChunk* Chunk = GetWorld()->SpawnActor<AGRTChunk>(ClassToSpawn, FTransform(Location), Params);
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
	else if (const UGRTSettings* S = GetDefault<UGRTSettings>())
	{
		Chunk->Resolution = FMath::Clamp(S->DefaultResolution, 32, 256);
	}
	else
	{
		Chunk->Resolution = 64;
		UE_LOG(LogTemp, Warning, TEXT("GRTSettings CDO not found; using default Resolution=64"));
	}

	// FinishSpawning() synchronously runs BeginPlay(), which (if !bInitialized) calls Initialize()
	// then Manager->RegisterChunk(this) — RegisterChunk() already adds this chunk to
	// ActiveChunkMap and calls SetMaterial(MasterMaterial) using the now-populated height/weight
	// data. Only fall back to doing it here if that didn't happen (e.g. subsystem lookup failed).
	Chunk->FinishSpawning(FTransform(Location));

	if (!ActiveChunkMap.Contains(Coord))
	{
		if (WorldOriginKind != EGRTChunkOrigin::ImportedGeoTIFF)
		{
			Chunk->SetMaterial(MasterMaterial);
		}
		ActiveChunkMap.Add(Coord, Chunk);
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	return Chunk;
}

// ---- Streaming: Load/Unload ----

void AGRTManager::LoadOrSpawnChunk(FIntPoint Coord)
{
	if (ActiveChunkMap.Contains(Coord)) return;
	if (!ShouldAllowChunkCoord(Coord)) return;
	AGRTChunk* Chunk = SpawnConfiguredChunk(Coord);
	if (!Chunk) return;

	// Try loading cached data (previously sculpted/painted chunk)
	FGRTChunkData CachedData;
	if (CachedChunkCoords.Contains(Coord) && LoadChunkFromCache(Coord, CachedData))
	{
		Chunk->LoadFromData(CachedData);
		// LoadFromData re-initializes the chunk when the cached resolution differs, which resizes
		// the pad buffers back to zero. Re-stamp after the load rather than before it.
		RefreshBuildingPadsForChunk(Chunk);
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Loaded chunk [%d,%d] from cache"), Coord.X, Coord.Y);
	}
	else if (WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF && ImportedChunkCoords.Contains(Coord) && ActiveGeoTIFFHeightSource.IsValid())
	{
		// No per-chunk edit cache yet -- this coordinate is streaming in from the DEM for the first
		// time. SpawnConfiguredChunk already seeded it with procedural noise above; on failure here
		// (out of the raster's footprint, sample miss, etc.) that procedural seed is simply left in
		// place rather than erroring, since ImportedChunkCoords is only ever a superset of what the
		// raster actually covers.
		if (TryPopulateChunkFromGeoTIFF(Chunk, Coord))
		{
			UE_LOG(LogTemp, Verbose, TEXT("Streaming: Populated chunk [%d,%d] from GeoTIFF"), Coord.X, Coord.Y);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Streaming: GeoTIFF sample failed for chunk [%d,%d], left procedural"), Coord.X, Coord.Y);
		}
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("Streaming: Spawned fresh chunk [%d,%d]"), Coord.X, Coord.Y);
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	SyncPopulationForChunk(Coord);
	PendingLoadQueue.Remove(Coord);
}

void AGRTManager::UnloadChunk(FIntPoint Coord)
{
	AGRTChunk** Found = ActiveChunkMap.Find(Coord);
	if (!Found || !(*Found)) return;

	AGRTChunk* Chunk = *Found;
	DespawnPopulationForChunk(Coord);

	// Persist dirty chunks
	if (DirtyChunkSet.Contains(Coord))
	{
		FGRTChunkData Data = Chunk->GetSerializedData();
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

void AGRTManager::UpdateStreaming(const TArray<FVector>& PlayerPositions)
{
	if (bStreamingPaused || PlayerPositions.Num() == 0) return;

	// Nothing has been decided yet, so there is no world to stream. Without this the mode choice is
	// cosmetic: BeginPlay spawns nothing, then this fills a ChunkLoadRadius diamond of procedural
	// chunks around the player anyway, and the player watches a sandbox build itself underneath the
	// dialog asking whether they want a sandbox.
	if (bAwaitingWorldModeChoice) return;

	const UGRTSettings* Settings = GetDefault<UGRTSettings>();
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

		// Queue chunks outside ALL players' unload radius (Only in procedural mode -- DEM datasets remain resident for macro top view)
		if (!ActiveGeoTIFFHeightSource.IsValid())
		{
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
	}

	ProcessStreamingQueues(PlayerPositions);
}

void AGRTManager::ProcessStreamingQueues(const TArray<FVector>& PlayerPositions)
{
	const UGRTSettings* Settings = GetDefault<UGRTSettings>();
	const int32 MaxOps = (ActiveGeoTIFFHeightSource.IsValid() && PendingLoadQueue.Num() > 0)
		? 16 // High-throughput for DEM macro-scale dataset loading
		: (Settings ? Settings->MaxChunkOpsPerTick : 2);
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

