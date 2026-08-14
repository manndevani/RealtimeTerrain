// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Settings/TerraDyneSettings.h"

UTerraDyneSettings::UTerraDyneSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("TerraDyne");

	// Default Paths.
	// These live in the PLUGIN's Content folder, which mounts as /TerraDyne/ — not /Game/, which is
	// the host project. They read /Game/TerraDyne/... for a long time, which resolves to nothing
	// unless a project happens to have its own Content/TerraDyne/ folder, so both loads silently
	// returned null and every dependent fallback quietly did nothing.
	MasterMaterialPath = FSoftObjectPath(TEXT("/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master"));
	HeightBrushMaterialPath = FSoftObjectPath(TEXT("/TerraDyne/Materials/Tools/M_HeightBrush.M_HeightBrush"));
	HUDWidgetPath = FSoftObjectPath();

	// Default Values
	DefaultChunkSize = 10000.0f;
	DefaultResolution = 128;

	// Performance Defaults
	LODDistanceThreshold = 50000.0f; // 500m
	CollisionDebounceTime = 0.2f;
	FoliageDebounceTime = 0.5f;
	FoliageCullStartDistance = 15000.0f;
	FoliageCullEndDistance = 40000.0f;
	ChunkShadowCullDistance = 30000.0f;

	// Streaming Defaults
	ChunkLoadRadius = 5;
	ChunkUnloadRadius = 7;
	MaxChunkOpsPerTick = 2;
	GridExtent = 10;
	ChunkSaveDir = TEXT("TerraDyne/ChunkCache");

	// Undo/Redo Defaults
	MaxUndoHistory = 20;

	// Multiplayer Defaults
	MaxBrushRPCsPerSecond = 30.0f;
	MaxBrushRadius = 10000.0f;
	MaxBrushStrength = 5000.0f;
}
