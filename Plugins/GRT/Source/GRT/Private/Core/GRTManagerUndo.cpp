// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// AGRTManager -- undo/redo.
//
// Fourth translation unit split out of GRTManager.cpp. Same members, same class, same API;
// only the file boundary is new. See GRTManagerPopulation.cpp for the first.
//
// What lives here: per-player stroke history. A stroke opens with BeginStroke, which snapshots
// the sculpt buffer of every chunk the brush radius touches, and closes with CommitStroke, which
// pushes that snapshot set onto the player's undo stack as a single entry. Undo/Redo swap entries
// between the two stacks and hand them to RestoreSnapshots.
//
// Stacks are keyed per APlayerController rather than global, so one player's undo cannot rewind
// another's edit.
//
// Two behaviours worth knowing before changing anything here:
//
//   - Entries referencing chunks that have since streamed out are silently skipped. Undo history
//     is not persisted alongside the chunk cache, so an undo can be a partial no-op after the
//     terrain it covered has been evicted.
//
//   - RestoreSnapshots re-broadcasts through SendChunkSculptSync rather than replicating the
//     restored buffer directly, because a full sculpt buffer overflows Unreal's maximum bunch
//     size. See the comment on Multicast_SyncChunkSculpt in GRTManager.h.

#include "Core/GRTManager.h"
#include "Core/GRTManagerInternal.h"
#include "Core/GRTSubsystem.h"
#include "Settings/GRTSettings.h"
#include "World/GRTChunk.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void AGRTManager::BeginStroke(FVector WorldLocation, float Radius, APlayerController* Controller)
{
	if (PendingStroke.IsSet() && PendingStrokeOwner != Controller)
	{
		// PendingStrokeOwner can be stale (its controller disconnected mid-stroke without ever
		// calling CommitStroke) -- IsValid() before dereferencing, not just a null check, since a
		// TWeakObjectPtr to a destroyed-but-not-yet-GC'd actor is non-null but unsafe to use.
		const FString PreviousOwnerName = PendingStrokeOwner.IsValid() ? PendingStrokeOwner->GetName() : TEXT("<disconnected controller>");
		UE_LOG(LogTemp, Warning,
			TEXT("BeginStroke: overwriting pending stroke from %s with stroke from %s — previous stroke discarded"),
			*PreviousOwnerName, *GetNameSafe(Controller));
	}
	PendingStroke.Reset();
	PendingStrokeOwner = Controller;

	FGRTUndoEntry Entry;
	for (AGRTChunk* Chunk : GetChunksInRadius(WorldLocation, Radius))
	{
		FGRTChunkSnapshot Snap;
		Snap.Coordinate = Chunk->GridCoordinate;
		Snap.SculptBuffer = Chunk->SculptBuffer;
		Entry.Snapshots.Add(Chunk->GridCoordinate, MoveTemp(Snap));
	}
	PendingStroke = MoveTemp(Entry);
}

void AGRTManager::CommitStroke(APlayerController* Controller)
{
	if (!PendingStroke.IsSet() || PendingStrokeOwner != Controller) return;

	TArray<FGRTUndoEntry>& Stack = UndoStacks.FindOrAdd(Controller);
	Stack.Add(MoveTemp(PendingStroke.GetValue()));
	PendingStroke.Reset();
	PendingStrokeOwner = nullptr;

	int32 MaxHistory = 20;
	if (const UGRTSettings* S = GetDefault<UGRTSettings>())
	{
		MaxHistory = S->MaxUndoHistory;
	}
	if (Stack.Num() > MaxHistory)
		Stack.RemoveAt(0, Stack.Num() - MaxHistory);

	// New stroke clears redo for this player
	RedoStacks.Remove(Controller);
}

void AGRTManager::RestoreSnapshots(const FGRTUndoEntry& Entry)
{
	TArray<FIntPoint> RestoredCoords;
	for (const auto& Pair : Entry.Snapshots)
	{
		AGRTChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		const FGRTChunkSnapshot& Snap = Pair.Value;

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

void AGRTManager::Undo(APlayerController* Controller)
{
	TArray<FGRTUndoEntry>* Stack = UndoStacks.Find(Controller);
	if (!Stack || Stack->Num() == 0) return;

	FGRTUndoEntry UndoEntry = Stack->Pop();

	// Snapshot current state for redo
	FGRTUndoEntry RedoEntry;
	for (const auto& Pair : UndoEntry.Snapshots)
	{
		AGRTChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FGRTChunkSnapshot CurrentSnap;
		CurrentSnap.Coordinate = Chunk->GridCoordinate;
		CurrentSnap.SculptBuffer = Chunk->SculptBuffer;
		RedoEntry.Snapshots.Add(Pair.Key, MoveTemp(CurrentSnap));
	}
	RedoStacks.FindOrAdd(Controller).Add(MoveTemp(RedoEntry));

	RestoreSnapshots(UndoEntry);

	// Multicast affected chunks to remote clients
	for (const auto& Pair : UndoEntry.Snapshots)
	{
		AGRTChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		SendChunkSculptSync(Chunk);
	}
}

void AGRTManager::Redo(APlayerController* Controller)
{
	TArray<FGRTUndoEntry>* Stack = RedoStacks.Find(Controller);
	if (!Stack || Stack->Num() == 0) return;

	FGRTUndoEntry RedoEntry = Stack->Pop();

	// Snapshot current state back to undo
	FGRTUndoEntry UndoEntry;
	for (const auto& Pair : RedoEntry.Snapshots)
	{
		AGRTChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		FGRTChunkSnapshot CurrentSnap;
		CurrentSnap.Coordinate = Chunk->GridCoordinate;
		CurrentSnap.SculptBuffer = Chunk->SculptBuffer;
		UndoEntry.Snapshots.Add(Pair.Key, MoveTemp(CurrentSnap));
	}
	UndoStacks.FindOrAdd(Controller).Add(MoveTemp(UndoEntry));

	RestoreSnapshots(RedoEntry);

	// Multicast affected chunks to remote clients
	for (const auto& Pair : RedoEntry.Snapshots)
	{
		AGRTChunk* Chunk = GetChunkAtCoord(Pair.Key);
		if (!Chunk) continue;
		SendChunkSculptSync(Chunk);
	}
}

void AGRTManager::ClearUndoRedoState()
{
	UndoStacks.Reset();
	RedoStacks.Reset();
	PendingStroke.Reset();
	PendingStrokeOwner = nullptr;
}
