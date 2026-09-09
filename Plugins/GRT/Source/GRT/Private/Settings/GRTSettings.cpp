// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Settings/GRTSettings.h"

UGRTSettings::UGRTSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("GRT");

	// Default Paths.
	// These live in the PLUGIN's Content folder, which mounts as /GRT/ — not /Game/, which is
	// the host project. They read /Game/GRT/... for a long time, which resolves to nothing
	// unless a project happens to have its own Content/GRT/ folder, so both loads silently
	// returned null and every dependent fallback quietly did nothing.
	//
	// The terrain default is the INSTANCE, not M_GRT_Master. The master leaves every ground
	// texture parameter (Grass/Dirt/Rock/Stones/Snow) null, and a null texture parameter samples as
	// the engine checkerboard — so defaulting to the master tiles the whole landscape with grey
	// squares. MI_GRT_Terrain is the same material with those five textures actually assigned.
	MasterMaterialPath = FSoftObjectPath(TEXT("/GRT/Materials/VHFM/MI_GRT_Terrain.MI_GRT_Terrain"));
	HeightBrushMaterialPath = FSoftObjectPath(TEXT("/GRT/Materials/Tools/M_HeightBrush.M_HeightBrush"));
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
	ChunkSaveDir = TEXT("GRT/ChunkCache");

	// Undo/Redo Defaults
	MaxUndoHistory = 20;

	// Multiplayer Defaults
	MaxBrushRPCsPerSecond = 30.0f;
	MaxBrushRadius = 10000.0f;
	MaxBrushStrength = 5000.0f;
}
