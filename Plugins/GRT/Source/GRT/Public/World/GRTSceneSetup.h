// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GRTSceneSetup.generated.h"

// Forward Declarations
class AGRTManager;
class UMaterialInterface;
class UGRTWorldPreset;

UENUM(BlueprintType)
enum class EGRTDemoTemplate : uint8
{
	Sandbox UMETA(DisplayName = "Sandbox"),
	SurvivalFramework UMETA(DisplayName = "Survival Framework"),
	AuthoredWorldConversion UMETA(DisplayName = "Authored World Conversion")
};

/**
 * A helper tool for ons-boarding. 
 * Drastically simplifies setting up a new GRT level.
 */
UCLASS(Blueprintable)
class GRT_API AGRTSceneSetup : public AActor
{
	GENERATED_BODY()
	
public:	
	AGRTSceneSetup();

	//--- Configuration Assets ---//
	UPROPERTY(EditAnywhere, Category = "GRT Setup")
	TObjectPtr<UMaterialInterface> DefaultLandscapeMaterial;

	UPROPERTY(EditAnywhere, Category = "GRT Setup")
	TObjectPtr<UMaterialInterface> HeightTool;

	UPROPERTY(EditAnywhere, Category = "GRT Setup")
	TObjectPtr<UMaterialInterface> WeightTool;

	UPROPERTY(EditAnywhere, Category = "GRT Setup")
	TSubclassOf<AGRTManager> ManagerClass;

	UPROPERTY(EditAnywhere, Category = "GRT Setup")
	TObjectPtr<UGRTWorldPreset> WorldPreset;

	UPROPERTY(EditAnywhere, Category = "GRT Setup")
	EGRTDemoTemplate DemoTemplate = EGRTDemoTemplate::Sandbox;

	//--- Actions ---//
	
	// The "One Button Solution"
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GRT Setup")
	void InitializeWorld();

private:
	void SpawnLighting();
};
