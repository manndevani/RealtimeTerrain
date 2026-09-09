// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/GRTWorldTypes.h"
#include "GRTWorldPreset.generated.h"

UCLASS(BlueprintType)
class GRT_API UGRTWorldPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGRTWorldPreset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	FName PresetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bSpawnDefaultChunksOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bSetupDefaultLightingOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bRefreshNavMeshOnTerrainChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bRefreshNavMeshOnPopulationChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	float NavigationDirtyBoundsPadding = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	FGRTProceduralWorldSettings ProceduralSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTBiomeOverlay> BiomeOverlays;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTPopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTAISpawnZone> AISpawnZones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTBuildPermissionZone> BuildPermissionZones;
};
