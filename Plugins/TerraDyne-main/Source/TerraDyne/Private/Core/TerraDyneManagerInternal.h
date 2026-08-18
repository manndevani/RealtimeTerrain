// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneManager.h"

/**
 * Small geometry helpers shared by ATerraDyneManager's translation units.
 *
 * These were file-local statics in TerraDyneManager.cpp. When the population code moved to its own
 * TU it took its callers with it, so the helpers had to become shared rather than duplicated.
 *
 * Private header on purpose: this is internal to the manager's implementation and is not part of
 * the plugin's public surface. `inline` rather than `static` so both TUs share one definition
 * instead of each getting a private copy.
 */
namespace TerraDyneWorldFramework
{
	inline FBox MakeChunkBounds(const ATerraDyneManager& Manager, const FIntPoint Coord, float ChunkSize, float ZExtent = 4000.0f)
	{
		const FVector Center = Manager.GetActorLocation() + FVector(Coord.X * ChunkSize, Coord.Y * ChunkSize, 0.0f);
		return FBox::BuildAABB(Center, FVector(ChunkSize * 0.5f, ChunkSize * 0.5f, ZExtent));
	}

	inline FTransform MakeWorldTransform(const FTransform& LocalTransform, const FTransform& ParentTransform)
	{
		return LocalTransform * ParentTransform;
	}

	inline int32 ChunkIndexFromLocalAxis(float LocalAxis, float ChunkSize)
	{
		if (ChunkSize <= KINDA_SMALL_NUMBER)
		{
			return 0;
		}

		return FMath::FloorToInt((LocalAxis + (ChunkSize * 0.5f)) / ChunkSize);
	}
}
