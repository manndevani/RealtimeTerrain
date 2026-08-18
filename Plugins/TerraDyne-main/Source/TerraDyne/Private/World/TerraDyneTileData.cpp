// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneTileData.h"

UTerraDyneTileData::UTerraDyneTileData()
{
	Resolution = 128;
	RealWorldSize = 10000.0f; // Default 100m
	BakedZScale = 100.0f;
	GridCoordinate = FIntPoint(0, 0);
}

int32 UTerraDyneTileData::GetMemoryFootprint() const
{
	return InitialHeightMap.Num() * sizeof(uint16);
}
