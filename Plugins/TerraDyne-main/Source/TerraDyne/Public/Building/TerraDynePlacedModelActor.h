// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "Core/TerraDyneAssetLibrarySubsystem.h"
#include "TerraDynePlacedModelActor.generated.h"

/**
 * An instantiated 3D model placed in the TerraDyne world, selectable and transformable via the Transform Gizmo.
 */
UCLASS()
class TERRADYNE_API ATerraDynePlacedModelActor : public AActor
{
	GENERATED_BODY()

public:
	ATerraDynePlacedModelActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	UPROPERTY(ReplicatedUsing = OnRep_AssetId, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FGuid AssetId;

	UPROPERTY(ReplicatedUsing = OnRep_ModelName, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FString ModelName;

	UPROPERTY(ReplicatedUsing = OnRep_CustomLabel, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FString CustomLabel;

	UPROPERTY(ReplicatedUsing = OnRep_Scale3D, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FVector ReplicatedScale3D = FVector(1.0f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	bool bIsVisibleInScene = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	bool bIsSelectedInEditor = false;

	/** Sets geometry, materials, and metadata from an imported asset. */
	void SetModelAsset(const FTerraDyneImportedModelAsset& Asset);

	/** Toggles editor selection visual highlight. */
	void SetSelectionState(bool bSelected);

	/** Shows or hides the actor in the scene without deleting it. */
	void SetVisibleInScene(bool bVisible);

	/** Synchronizes local scale changes to replicated scale property. */
	void NotifyScaleChanged();

protected:
	UFUNCTION()
	void OnRep_Scale3D();

	UFUNCTION()
	void OnRep_AssetId();

	UFUNCTION()
	void OnRep_ModelName();

	UFUNCTION()
	void OnRep_CustomLabel();

private:
	void ApplyMaterials(const TArray<FTerraDyneImportedMaterial>& Materials);
};
