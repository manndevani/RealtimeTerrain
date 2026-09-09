// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GRTSettings.generated.h"

/**
 * Global configuration for the GRT plugin.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "GRT Settings"))
class GRT_API UGRTSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGRTSettings();

	//--- Asset Paths ---//
	UPROPERTY(Config, EditAnywhere, Category = "Asset Paths", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath MasterMaterialPath;

	UPROPERTY(Config, EditAnywhere, Category = "Asset Paths", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath HeightBrushMaterialPath;

	UPROPERTY(Config, EditAnywhere, Category = "Asset Paths", meta = (AllowedClasses = "/Script/UMG.WidgetBlueprint"))
	FSoftObjectPath HUDWidgetPath;

	//--- Defaults ---//
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (ClampMin = "100.0"))
	float DefaultChunkSize;

	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (ClampMin = "32", ClampMax = "1024"))
	int32 DefaultResolution;

	//--- Performance ---//
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ToolTip = "Distance in World Units at which chunks disable complex collision."))
	float LODDistanceThreshold;

	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ToolTip = "Time in seconds to wait after an edit before rebuilding physics."))
	float CollisionDebounceTime;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Seconds to wait after a sculpt/paint edit before refreshing terrain-following foliage.", ClampMin = "0.05"))
	float FoliageDebounceTime;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Distance in World Units at which transferred tree/prop foliage instances start fading out.", ClampMin = "0.0"))
	float FoliageCullStartDistance;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Distance in World Units beyond which transferred tree/prop foliage instances are fully culled.", ClampMin = "0.0"))
	float FoliageCullEndDistance;

	UPROPERTY(Config, EditAnywhere, Category = "Performance",
	    meta = (ToolTip = "Distance in World Units beyond which chunk terrain stops casting dynamic shadows.", ClampMin = "0.0"))
	float ChunkShadowCullDistance;

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

	//--- Undo/Redo ---//
	UPROPERTY(Config, EditAnywhere, Category = "Undo/Redo",
	    meta = (ToolTip = "Maximum number of undo entries stored per player.", ClampMin = "1", ClampMax = "100"))
	int32 MaxUndoHistory;

	//--- Multiplayer ---//
	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
	    meta = (ToolTip = "Max brush RPCs accepted per player per second (server-side rate limit).", ClampMin = "1.0", ClampMax = "120.0"))
	float MaxBrushRPCsPerSecond;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
	    meta = (ToolTip = "Maximum brush radius the server will accept.", ClampMin = "100.0"))
	float MaxBrushRadius;

	UPROPERTY(Config, EditAnywhere, Category = "Multiplayer",
	    meta = (ToolTip = "Maximum brush strength the server will accept.", ClampMin = "0.0"))
	float MaxBrushStrength;
};
