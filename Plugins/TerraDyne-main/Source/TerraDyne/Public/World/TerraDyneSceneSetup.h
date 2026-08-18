// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerraDyneSceneSetup.generated.h"

// Forward Declarations
class ATerraDyneManager;
class UMaterialInterface;
class UTerraDyneWorldPreset;

UENUM(BlueprintType)
enum class ETerraDyneDemoTemplate : uint8
{
	FullFeatureShowcase UMETA(DisplayName = "Full Feature Showcase"),
	Sandbox UMETA(DisplayName = "Sandbox"),
	SurvivalFramework UMETA(DisplayName = "Survival Framework"),
	AuthoredWorldConversion UMETA(DisplayName = "Authored World Conversion")
};

/**
 * A helper tool for ons-boarding. 
 * Drastically simplifies setting up a new TerraDyne level.
 */
UCLASS(Blueprintable)
class TERRADYNE_API ATerraDyneSceneSetup : public AActor
{
	GENERATED_BODY()
	
public:	
	ATerraDyneSceneSetup();

	//--- Configuration Assets ---//
	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TObjectPtr<UMaterialInterface> DefaultLandscapeMaterial;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TObjectPtr<UMaterialInterface> HeightTool;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TObjectPtr<UMaterialInterface> WeightTool;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TSubclassOf<ATerraDyneManager> ManagerClass;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TObjectPtr<UTerraDyneWorldPreset> WorldPreset;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	ETerraDyneDemoTemplate DemoTemplate = ETerraDyneDemoTemplate::Sandbox;

	//--- Actions ---//
	
	// The "One Button Solution"
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne Setup")
	void InitializeWorld();

private:
	void SpawnLighting();
};
