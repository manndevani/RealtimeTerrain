// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// ATerraDyneManager -- persistent world population.
//
// Split out of TerraDyneManager.cpp, which had grown to 3,408 lines covering eight unrelated
// concerns. These are the same ATerraDyneManager members as before, in a translation unit of
// their own; nothing about the class or its API changed. The point is to make one seam explicit
// and reviewable before anything is actually decoupled.
//
// What lives here: the persistent population registry -- registering placed actors, spawning from
// descriptors and spawn rules, harvest/destroy/regrowth state, and per-chunk population sync as
// chunks stream in and out.
//
// What it still reaches back into the manager for, and why this is not yet a separate class:
//     chunk lookup      GetChunkAtCoord, ActiveChunkMap, WorldToChunkCoord, GlobalChunkSize
//     procedural state  EnsureChunkFrameworkState, FindProceduralChunkState, MakeChunkSeed
//     gameplay hooks    RefreshNavigationForBounds
// Lifting those into an interface is the next step; see the god-object note in the analysis.

#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneManagerInternal.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneSaveGame.h"
#include "Core/TerraDyneWorldPreset.h"
#include "Settings/TerraDyneSettings.h"
#include "World/TerraDyneChunk.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"

void ATerraDyneManager::DestroyTrackedPopulationActors()
{
	TArray<TObjectPtr<AActor>> ActorsToDestroy;
	ActorsToDestroy.Reserve(PopulationActorById.Num());
	for (const auto& Pair : PopulationActorById)
	{
		if (AActor* Actor = Pair.Value.Get())
		{
			ActorsToDestroy.Add(Actor);
		}
	}

	PopulationIdByActor.Reset();
	PopulationActorById.Reset();
	PopulationDestroyInProgress.Reset();

	for (AActor* Actor : ActorsToDestroy)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
}

FTerraDynePopulationDescriptor ATerraDyneManager::MakeDescriptorFromActor(
	AActor* Actor,
	ETerraDynePopulationKind Kind,
	bool bAllowRegrowth,
	float RegrowthDelaySeconds,
	bool bSnapToTerrain) const
{
	FTerraDynePopulationDescriptor Descriptor;
	Descriptor.Kind = Kind;
	Descriptor.TypeId = Actor ? Actor->GetClass()->GetFName() : NAME_None;
	Descriptor.ActorClass = Actor ? Actor->GetClass() : nullptr;
	Descriptor.bReplicates = true;
	Descriptor.bAttachToChunk = false;
	Descriptor.bSnapToTerrain = bSnapToTerrain;
	Descriptor.bAllowRegrowth = bAllowRegrowth;
	Descriptor.RegrowthDelaySeconds = RegrowthDelaySeconds;

	if (const AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
	{
		if (const UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent())
		{
			Descriptor.StaticMesh = StaticMeshComponent->GetStaticMesh();
			Descriptor.MaterialOverride = StaticMeshComponent->GetMaterial(0);
		}
	}

	return Descriptor;
}

FGuid ATerraDyneManager::RegisterPersistentActor(
	AActor* Actor,
	ETerraDynePopulationKind Kind,
	bool bAllowRegrowth,
	float RegrowthDelaySeconds,
	bool bSnapToTerrain)
{
	if (!HasAuthority() || !Actor)
	{
		return FGuid();
	}

	if (const FGuid* ExistingId = PopulationIdByActor.Find(Actor))
	{
		return *ExistingId;
	}

	FTerraDynePopulationDescriptor Descriptor =
		MakeDescriptorFromActor(Actor, Kind, bAllowRegrowth, RegrowthDelaySeconds, bSnapToTerrain);
	const FTransform WorldTransform = Actor->GetActorTransform();
	FGuid PopulationId = FGuid::NewGuid();

	FTerraDynePersistentPopulationEntry Entry;
	Entry.PopulationId = PopulationId;
	Entry.Descriptor = Descriptor;
	Entry.ChunkCoord = WorldToChunkCoord(WorldTransform.GetLocation());
	Entry.LocalTransform = WorldTransform.GetRelativeTransform(GetActorTransform());
	Entry.State = ETerraDynePopulationState::Active;
	Entry.CachedBiomeTag = GetBiomeTagAtLocation(WorldTransform.GetLocation());
	Entry.Descriptor.BiomeTag = Entry.CachedBiomeTag;

	if (Descriptor.bSnapToTerrain)
	{
		if (ATerraDyneChunk* Chunk = GetChunkAtCoord(Entry.ChunkCoord))
		{
			const FVector ChunkLocal = WorldTransform.GetLocation() - Chunk->GetActorLocation();
			const float TerrainZ = Chunk->GetActorLocation().Z +
				Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y, 0.0f));
			Entry.Descriptor.TerrainOffset = WorldTransform.GetLocation().Z - TerrainZ;
		}
	}

	const int32 NewIndex = PersistentPopulationEntries.Add(MoveTemp(Entry));
	PopulationEntryIndexById.Add(PersistentPopulationEntries[NewIndex].PopulationId, NewIndex);
	BindPopulationActor(PersistentPopulationEntries[NewIndex].PopulationId, Actor);
	if (HasAuthority() && Actor->IsActorInitialized())
	{
		Actor->SetReplicates(PersistentPopulationEntries[NewIndex].Descriptor.bReplicates);
	}
	BroadcastPopulationChanged(PersistentPopulationEntries[NewIndex], FName(TEXT("Registered")));
	RefreshNavigationForBounds(Actor->GetComponentsBoundingBox(true), true);
	return PopulationId;
}

FGuid ATerraDyneManager::PlacePersistentActorFromDescriptor(
	const FTerraDynePopulationDescriptor& Descriptor,
	const FTransform& WorldTransform,
	FName SourceRuleId)
{
	if (!HasAuthority())
	{
		return FGuid();
	}

	if (Descriptor.ActorClass.IsNull() && Descriptor.StaticMesh.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyne: PlacePersistentActorFromDescriptor called without a class or mesh."));
		return FGuid();
	}

	FTerraDynePersistentPopulationEntry Entry;
	Entry.PopulationId = FGuid::NewGuid();
	Entry.Descriptor = Descriptor;
	Entry.ChunkCoord = WorldToChunkCoord(WorldTransform.GetLocation());
	Entry.LocalTransform = WorldTransform.GetRelativeTransform(GetActorTransform());
	Entry.State = ETerraDynePopulationState::Active;
	Entry.SourceRuleId = SourceRuleId;
	Entry.bSpawnedFromRule = SourceRuleId != NAME_None;
	Entry.CachedBiomeTag = Descriptor.BiomeTag != NAME_None ? Descriptor.BiomeTag : GetBiomeTagAtLocation(WorldTransform.GetLocation());
	Entry.Descriptor.BiomeTag = Entry.CachedBiomeTag;

	if (Entry.Descriptor.TypeId == NAME_None)
	{
		if (!Descriptor.ActorClass.IsNull())
		{
			if (UClass* LoadedClass = Descriptor.ActorClass.LoadSynchronous())
			{
				Entry.Descriptor.TypeId = LoadedClass->GetFName();
			}
		}
		else if (UStaticMesh* LoadedMesh = Descriptor.StaticMesh.LoadSynchronous())
		{
			Entry.Descriptor.TypeId = LoadedMesh->GetFName();
		}
	}

	const int32 NewIndex = PersistentPopulationEntries.Add(MoveTemp(Entry));
	PopulationEntryIndexById.Add(PersistentPopulationEntries[NewIndex].PopulationId, NewIndex);
	SpawnPopulationActor(PersistentPopulationEntries[NewIndex]);
	BroadcastPopulationChanged(PersistentPopulationEntries[NewIndex], FName(TEXT("Placed")));
	RefreshNavigationForBounds(
		TerraDyneWorldFramework::MakeChunkBounds(*this, PersistentPopulationEntries[NewIndex].ChunkCoord, GlobalChunkSize),
		true);
	return PersistentPopulationEntries[NewIndex].PopulationId;
}

FGuid ATerraDyneManager::PlacePersistentActor(
	TSubclassOf<AActor> ActorClass,
	const FTransform& WorldTransform,
	ETerraDynePopulationKind Kind,
	bool bSnapToTerrain,
	float TerrainOffset,
	bool bAllowRegrowth,
	float RegrowthDelaySeconds)
{
	FTerraDynePopulationDescriptor Descriptor;
	Descriptor.Kind = Kind;
	Descriptor.ActorClass = ActorClass;
	Descriptor.TypeId = ActorClass ? ActorClass->GetFName() : NAME_None;
	Descriptor.bReplicates = true;
	Descriptor.bSnapToTerrain = bSnapToTerrain;
	Descriptor.TerrainOffset = TerrainOffset;
	Descriptor.bAllowRegrowth = bAllowRegrowth;
	Descriptor.RegrowthDelaySeconds = RegrowthDelaySeconds;
	return PlacePersistentActorFromDescriptor(Descriptor, WorldTransform);
}

void ATerraDyneManager::BindPopulationActor(const FGuid& PopulationId, AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	PopulationActorById.Add(PopulationId, Actor);
	PopulationIdByActor.Add(Actor, PopulationId);
	Actor->OnDestroyed.RemoveDynamic(this, &ATerraDyneManager::HandlePopulationActorDestroyed);
	Actor->OnDestroyed.AddUniqueDynamic(this, &ATerraDyneManager::HandlePopulationActorDestroyed);
}

AActor* ATerraDyneManager::SpawnPopulationActor(const FTerraDynePersistentPopulationEntry& Entry)
{
	if (!HasAuthority())
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(Entry.PopulationId))
	{
		if (ExistingActor->IsValid())
		{
			return ExistingActor->Get();
		}
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Entry.ChunkCoord);
	if (!Chunk || !GetWorld())
	{
		return nullptr;
	}

	FTransform WorldTransform = TerraDyneWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
	if (Entry.Descriptor.bSnapToTerrain)
	{
		FVector WorldLocation = WorldTransform.GetLocation();
		const FVector ChunkLocal = WorldLocation - Chunk->GetActorLocation();
		WorldLocation.Z = Chunk->GetActorLocation().Z +
			Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y, 0.0f)) +
			Entry.Descriptor.TerrainOffset;
		WorldTransform.SetLocation(WorldLocation);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedActor = nullptr;
	if (UClass* DesiredClass = Entry.Descriptor.ActorClass.LoadSynchronous())
	{
		SpawnedActor = GetWorld()->SpawnActor<AActor>(DesiredClass, WorldTransform, SpawnParams);
	}
	else if (UStaticMesh* StaticMesh = Entry.Descriptor.StaticMesh.LoadSynchronous())
	{
		if (AStaticMeshActor* StaticMeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			WorldTransform,
			SpawnParams))
		{
			if (UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent())
			{
				StaticMeshComponent->SetStaticMesh(StaticMesh);
				if (UMaterialInterface* MaterialOverride = Entry.Descriptor.MaterialOverride.LoadSynchronous())
				{
					StaticMeshComponent->SetMaterial(0, MaterialOverride);
				}
			}
			SpawnedActor = StaticMeshActor;
		}
	}

	if (!SpawnedActor)
	{
		return nullptr;
	}

	if (UStaticMesh* StaticMesh = Entry.Descriptor.StaticMesh.LoadSynchronous())
	{
		if (UStaticMeshComponent* StaticMeshComponent = SpawnedActor->FindComponentByClass<UStaticMeshComponent>())
		{
			StaticMeshComponent->SetStaticMesh(StaticMesh);
			if (UMaterialInterface* MaterialOverride = Entry.Descriptor.MaterialOverride.LoadSynchronous())
			{
				StaticMeshComponent->SetMaterial(0, MaterialOverride);
			}
		}
	}

	if (SpawnedActor->IsActorInitialized())
	{
		SpawnedActor->SetReplicates(Entry.Descriptor.bReplicates);
	}
	SpawnedActor->Tags.AddUnique(FName(TEXT("TerraDynePersistent")));
	if (Entry.Descriptor.TypeId != NAME_None)
	{
		SpawnedActor->Tags.AddUnique(Entry.Descriptor.TypeId);
	}
	for (const FName& Tag : Entry.Descriptor.Tags)
	{
		SpawnedActor->Tags.AddUnique(Tag);
	}

	if (Entry.Descriptor.bAttachToChunk && SpawnedActor->GetRootComponent() && Chunk->GetRootComponent())
	{
		SpawnedActor->AttachToComponent(Chunk->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}

	BindPopulationActor(Entry.PopulationId, SpawnedActor);
	return SpawnedActor;
}

void ATerraDyneManager::SetPopulationStateInternal(
	const FGuid& PopulationId,
	ETerraDynePopulationState NewState,
	FName Reason)
{
	const int32* EntryIndex = PopulationEntryIndexById.Find(PopulationId);
	if (!EntryIndex || !PersistentPopulationEntries.IsValidIndex(*EntryIndex))
	{
		return;
	}

	FTerraDynePersistentPopulationEntry& Entry = PersistentPopulationEntries[*EntryIndex];
	if (Entry.State == NewState && NewState != ETerraDynePopulationState::Active)
	{
		return;
	}

	FBox DirtyBounds = TerraDyneWorldFramework::MakeChunkBounds(*this, Entry.ChunkCoord, GlobalChunkSize);
	if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(PopulationId))
	{
		if (ExistingActor->IsValid())
		{
			DirtyBounds = ExistingActor->Get()->GetComponentsBoundingBox(true);
		}
	}

	Entry.State = NewState;
	Entry.RemainingRegrowthTimeSeconds =
		NewState == ETerraDynePopulationState::Regrowing
			? FMath::Max(0.05f, Entry.Descriptor.RegrowthDelaySeconds)
			: 0.0f;

	if (NewState == ETerraDynePopulationState::Active)
	{
		SyncPopulationForChunk(Entry.ChunkCoord);
	}
	else if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(PopulationId))
	{
		if (ExistingActor->IsValid())
		{
			PopulationDestroyInProgress.Add(PopulationId);
			ExistingActor->Get()->Destroy();
		}
	}

	BroadcastPopulationChanged(Entry, Reason);
	RefreshNavigationForBounds(DirtyBounds, true);
}

bool ATerraDyneManager::HarvestPersistentPopulation(const FGuid& PopulationId)
{
	const int32* EntryIndex = PopulationEntryIndexById.Find(PopulationId);
	if (!EntryIndex || !PersistentPopulationEntries.IsValidIndex(*EntryIndex))
	{
		return false;
	}

	const FTerraDynePersistentPopulationEntry& Entry = PersistentPopulationEntries[*EntryIndex];
	const ETerraDynePopulationState NewState =
		Entry.Descriptor.bAllowRegrowth && Entry.Descriptor.RegrowthDelaySeconds > 0.0f
			? ETerraDynePopulationState::Regrowing
			: ETerraDynePopulationState::Harvested;
	SetPopulationStateInternal(PopulationId, NewState, FName(TEXT("Harvested")));
	return true;
}

bool ATerraDyneManager::SetPersistentPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed)
{
	SetPopulationStateInternal(
		PopulationId,
		bDestroyed ? ETerraDynePopulationState::Destroyed : ETerraDynePopulationState::Active,
		bDestroyed ? FName(TEXT("Destroyed")) : FName(TEXT("Restored")));
	return PopulationEntryIndexById.Contains(PopulationId);
}

void ATerraDyneManager::GeneratePopulationForChunk(FIntPoint Coord, ATerraDyneChunk* Chunk)
{
	if (!HasAuthority() || !Chunk || !ProceduralWorldSettings.bGeneratePopulationFromRules)
	{
		return;
	}

	FTerraDyneProceduralChunkState* ChunkState = FindProceduralChunkState(Coord);
	if (!ChunkState)
	{
		EnsureChunkFrameworkState(Coord, Chunk);
		ChunkState = FindProceduralChunkState(Coord);
	}

	if (!ChunkState || ChunkState->bPopulationGenerated)
	{
		return;
	}

	const bool bAuthoredChunk = ChunkState->bIsAuthoredChunk;
	const int32 DistanceFromAuthored = ChunkState->DistanceFromAuthored;
	const float HalfSize = Chunk->WorldSize * 0.5f;

	for (const FTerraDynePopulationSpawnRule& Rule : PopulationSpawnRules)
	{
		if (Rule.RuleId == NAME_None)
		{
			continue;
		}

		if ((bAuthoredChunk && !Rule.bAllowOnAuthoredChunks) || (!bAuthoredChunk && !Rule.bAllowOnProceduralChunks))
		{
			continue;
		}

		if (DistanceFromAuthored < Rule.MinDistanceFromAuthored || DistanceFromAuthored > Rule.MaxDistanceFromAuthored)
		{
			continue;
		}

		if (Rule.RequiredBiomeTag != NAME_None &&
			Rule.RequiredBiomeTag != ChunkState->PrimaryBiomeTag &&
			!ChunkState->OverlayBiomeTags.Contains(Rule.RequiredBiomeTag))
		{
			continue;
		}

		if (Rule.Descriptor.ActorClass.IsNull() && Rule.Descriptor.StaticMesh.IsNull())
		{
			continue;
		}

		FRandomStream Stream(MakeChunkSeed(Coord) ^ GetTypeHash(Rule.RuleId));
		const int32 MinCount = FMath::Max(0, Rule.MinInstancesPerChunk);
		const int32 MaxCount = FMath::Max(MinCount, Rule.MaxInstancesPerChunk);
		const int32 SpawnCount = Stream.RandRange(MinCount, MaxCount);
		TArray<FVector2D> SpawnedLocalPoints;

		for (int32 InstanceIndex = 0; InstanceIndex < SpawnCount; ++InstanceIndex)
		{
			bool bPlaced = false;
			for (int32 Attempt = 0; Attempt < 12 && !bPlaced; ++Attempt)
			{
				const float LocalX = Stream.FRandRange(-HalfSize + Rule.ChunkPadding, HalfSize - Rule.ChunkPadding);
				const float LocalY = Stream.FRandRange(-HalfSize + Rule.ChunkPadding, HalfSize - Rule.ChunkPadding);
				const FVector2D Candidate(LocalX, LocalY);

				bool bOverlapsExisting = false;
				for (const FVector2D& ExistingPoint : SpawnedLocalPoints)
				{
					if (FVector2D::Distance(ExistingPoint, Candidate) < Rule.MinSpacing)
					{
						bOverlapsExisting = true;
						break;
					}
				}

				if (bOverlapsExisting)
				{
					continue;
				}

				const float LocalZ = Rule.Descriptor.bSnapToTerrain
					? Chunk->GetHeightAtLocation(FVector(LocalX, LocalY, 0.0f)) + Rule.Descriptor.TerrainOffset
					: Rule.Descriptor.TerrainOffset;

				FTransform WorldTransform(
					FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f),
					Chunk->GetActorTransform().TransformPosition(FVector(LocalX, LocalY, LocalZ)),
					FVector(1.0f));

				FTerraDynePopulationDescriptor Descriptor = Rule.Descriptor;
				Descriptor.Kind = Rule.Descriptor.Kind == ETerraDynePopulationKind::Prop
					? ETerraDynePopulationKind::ProceduralSpawn
					: Rule.Descriptor.Kind;
				Descriptor.BiomeTag = ChunkState->PrimaryBiomeTag;
				PlacePersistentActorFromDescriptor(Descriptor, WorldTransform, Rule.RuleId);
				SpawnedLocalPoints.Add(Candidate);
				bPlaced = true;
			}
		}
	}

	ChunkState->bPopulationGenerated = true;
}

void ATerraDyneManager::SyncPopulationForChunk(FIntPoint Coord)
{
	if (!HasAuthority())
	{
		return;
	}

	ATerraDyneChunk* Chunk = GetChunkAtCoord(Coord);
	if (!Chunk)
	{
		return;
	}

	EnsureChunkFrameworkState(Coord, Chunk);
	GeneratePopulationForChunk(Coord, Chunk);

	for (FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
	{
		if (Entry.ChunkCoord != Coord)
		{
			continue;
		}

		if (Entry.State == ETerraDynePopulationState::Active)
		{
			if (AActor* Actor = SpawnPopulationActor(Entry))
			{
				FTransform WorldTransform = TerraDyneWorldFramework::MakeWorldTransform(Entry.LocalTransform, GetActorTransform());
				if (Entry.Descriptor.bSnapToTerrain)
				{
					FVector WorldLocation = WorldTransform.GetLocation();
					const FVector ChunkLocal = WorldLocation - Chunk->GetActorLocation();
					WorldLocation.Z = Chunk->GetActorLocation().Z +
						Chunk->GetHeightAtLocation(FVector(ChunkLocal.X, ChunkLocal.Y, 0.0f)) +
						Entry.Descriptor.TerrainOffset;
					WorldTransform.SetLocation(WorldLocation);
				}

				Actor->SetActorTransform(WorldTransform);
			}
		}
		else if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(Entry.PopulationId))
		{
			if (ExistingActor->IsValid())
			{
				PopulationDestroyInProgress.Add(Entry.PopulationId);
				ExistingActor->Get()->Destroy();
			}
		}
	}
}

void ATerraDyneManager::DespawnPopulationForChunk(FIntPoint Coord)
{
	if (!HasAuthority())
	{
		return;
	}

	for (const FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
	{
		if (Entry.ChunkCoord != Coord)
		{
			continue;
		}

		if (const TWeakObjectPtr<AActor>* ExistingActor = PopulationActorById.Find(Entry.PopulationId))
		{
			if (ExistingActor->IsValid())
			{
				PopulationDestroyInProgress.Add(Entry.PopulationId);
				ExistingActor->Get()->Destroy();
			}
		}
	}
}

void ATerraDyneManager::HandlePopulationActorDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor)
	{
		return;
	}

	const FGuid* PopulationId = PopulationIdByActor.Find(DestroyedActor);
	if (!PopulationId)
	{
		return;
	}

	const FGuid PopulationGuid = *PopulationId;
	const FBox DirtyBounds = DestroyedActor->GetComponentsBoundingBox(true);
	PopulationIdByActor.Remove(DestroyedActor);
	PopulationActorById.Remove(PopulationGuid);

	if (PopulationDestroyInProgress.Remove(PopulationGuid) > 0)
	{
		return;
	}

	const int32* EntryIndex = PopulationEntryIndexById.Find(PopulationGuid);
	if (!EntryIndex || !PersistentPopulationEntries.IsValidIndex(*EntryIndex))
	{
		return;
	}

	FTerraDynePersistentPopulationEntry& Entry = PersistentPopulationEntries[*EntryIndex];
	if (Entry.Descriptor.bAllowRegrowth && Entry.Descriptor.RegrowthDelaySeconds > 0.0f)
	{
		Entry.State = ETerraDynePopulationState::Regrowing;
		Entry.RemainingRegrowthTimeSeconds = Entry.Descriptor.RegrowthDelaySeconds;
	}
	else
	{
		Entry.State = Entry.Descriptor.Kind == ETerraDynePopulationKind::Harvestable
			? ETerraDynePopulationState::Harvested
			: ETerraDynePopulationState::Destroyed;
		Entry.RemainingRegrowthTimeSeconds = 0.0f;
	}

	BroadcastPopulationChanged(Entry, FName(TEXT("Destroyed")));
	RefreshNavigationForBounds(DirtyBounds, true);
}

void ATerraDyneManager::TickPopulationState(float DeltaTime)
{
	TArray<FIntPoint> ChunksToSync;

	for (FTerraDynePersistentPopulationEntry& Entry : PersistentPopulationEntries)
	{
		if (Entry.State != ETerraDynePopulationState::Regrowing)
		{
			continue;
		}

		Entry.RemainingRegrowthTimeSeconds = FMath::Max(0.0f, Entry.RemainingRegrowthTimeSeconds - DeltaTime);
		if (Entry.RemainingRegrowthTimeSeconds > 0.0f)
		{
			continue;
		}

		Entry.State = ETerraDynePopulationState::Active;
		Entry.RemainingRegrowthTimeSeconds = 0.0f;
		ChunksToSync.AddUnique(Entry.ChunkCoord);
		BroadcastPopulationChanged(Entry, FName(TEXT("Regrown")));
	}

	for (const FIntPoint& Coord : ChunksToSync)
	{
		SyncPopulationForChunk(Coord);
	}
}

void ATerraDyneManager::RefreshPopulationForLoadedChunks()
{
	if (!HasAuthority())
	{
		return;
	}

	for (const auto& Pair : ActiveChunkMap)
	{
		if (Pair.Value)
		{
			SyncPopulationForChunk(Pair.Key);
		}
	}
}

void ATerraDyneManager::BroadcastPopulationChanged(
	const FTerraDynePersistentPopulationEntry& Entry,
	FName Reason)
{
	FTerraDynePopulationChangeEvent Event;
	Event.PopulationId = Entry.PopulationId;
	Event.TypeId = Entry.Descriptor.TypeId;
	Event.Kind = Entry.Descriptor.Kind;
	Event.State = Entry.State;
	Event.Reason = Reason;
	OnPopulationChanged.Broadcast(Event);
}
