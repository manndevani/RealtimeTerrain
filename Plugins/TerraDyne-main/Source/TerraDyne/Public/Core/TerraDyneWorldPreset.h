// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/TerraDyneWorldTypes.h"
#include "TerraDyneWorldPreset.generated.h"

UCLASS(BlueprintType)
class TERRADYNE_API UTerraDyneWorldPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UTerraDyneWorldPreset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FName PresetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bSpawnDefaultChunksOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bSetupDefaultLightingOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bRefreshNavMeshOnTerrainChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	bool bRefreshNavMeshOnPopulationChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	float NavigationDirtyBoundsPadding = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	FTerraDyneProceduralWorldSettings ProceduralSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneBiomeOverlay> BiomeOverlays;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDynePopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneAISpawnZone> AISpawnZones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TArray<FTerraDyneBuildPermissionZone> BuildPermissionZones;
};
