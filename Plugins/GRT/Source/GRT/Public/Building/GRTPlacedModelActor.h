// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "Core/GRTAssetLibrarySubsystem.h"
#include "GRTPlacedModelActor.generated.h"

/**
 * An instantiated 3D model placed in the GRT world, selectable and transformable via the Transform Gizmo.
 */
UCLASS()
class GRT_API AGRTPlacedModelActor : public AActor
{
	GENERATED_BODY()

public:
	AGRTPlacedModelActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	UPROPERTY(ReplicatedUsing = OnRep_AssetId, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FGuid AssetId;

	UPROPERTY(ReplicatedUsing = OnRep_ModelName, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FString ModelName;

	/**
	 * Where this model was imported from, replicated alongside AssetId.
	 *
	 * Imported geometry lives in no package and cannot travel over the wire, and AssetId alone is
	 * meaningless on a machine that never ran the import -- such a client resolved nothing and drew
	 * an empty actor. With the path, it can import the file for itself under the same id. It still
	 * needs the file to exist locally; that limitation is shared with the GeoTIFF import path.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_SourceFilePath, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FString SourceFilePath;

	UPROPERTY(ReplicatedUsing = OnRep_CustomLabel, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FString CustomLabel;

	UPROPERTY(ReplicatedUsing = OnRep_Scale3D, VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	FVector ReplicatedScale3D = FVector(1.0f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	bool bIsVisibleInScene = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Model")
	bool bIsSelectedInEditor = false;

	/** Sets geometry, materials, and metadata from an imported asset. */
	void SetModelAsset(const FGRTImportedModelAsset& Asset);

	/** Toggles editor selection visual highlight. */
	void SetSelectionState(bool bSelected);

	/** Shows or hides the actor in the scene without deleting it. */
	void SetVisibleInScene(bool bVisible);

	/** Synchronizes local scale changes to replicated scale property. */
	void NotifyScaleChanged();

	/**
	 * Binds this actor's geometry from the local asset library, importing SourceFilePath under
	 * AssetId first if this machine has not seen it. Safe to call repeatedly; no-ops once bound.
	 */
	void ResolveModelAsset();

	//--- Serialization ---
	FGRTPlacedModelData ToData() const;
	void ApplyData(const FGRTPlacedModelData& Data);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Scale3D();

	UFUNCTION()
	void OnRep_AssetId();

	UFUNCTION()
	void OnRep_SourceFilePath();

	UFUNCTION()
	void OnRep_ModelName();

	UFUNCTION()
	void OnRep_CustomLabel();

private:
	void ApplyMaterials(const TArray<FGRTImportedMaterial>& Materials);
};
