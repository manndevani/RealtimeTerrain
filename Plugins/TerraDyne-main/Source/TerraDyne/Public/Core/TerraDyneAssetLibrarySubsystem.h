// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IO/TerraDyneModelImporter.h"
#include "TerraDyneAssetLibrarySubsystem.generated.h"

/**
 * Metadata and cached geometry for an imported 3D model asset in the library.
 */
USTRUCT(BlueprintType)
struct TERRADYNE_API FTerraDyneImportedModelAsset
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	FGuid AssetId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	FString AssetName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	FString SourceFilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	FVector Dimensions = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	int32 VertexCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	int32 TriangleCount = 0;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Asset")
	TObjectPtr<UTexture2D> ThumbnailTexture = nullptr;

	FDynamicMesh3 Mesh;
	TArray<FTerraDyneImportedMaterial> Materials;
	FBox3d BoundingBox;
};

DECLARE_MULTICAST_DELEGATE(FOnTerraDyneAssetLibraryChanged);

/**
 * World subsystem managing imported 3D model assets, catalog registration, thumbnails, and placement palettes.
 */
UCLASS()
class TERRADYNE_API UTerraDyneAssetLibrarySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Imports a 3D model file (.obj, .stl, .ply) into the asset library. */
	FGuid ImportModelFromFile(const FString& FilePath, FString& OutError);

	/** Batch imports all supported 3D models from a directory. */
	int32 ImportDirectory(const FString& DirectoryPath);

	/** Removes an asset from the library. */
	bool RemoveAsset(const FGuid& AssetId);

	/** Retrieves all currently loaded assets. */
	const TArray<FTerraDyneImportedModelAsset>& GetAssets() const { return LoadedAssets; }

	/** Finds an asset by its unique GUID. */
	const FTerraDyneImportedModelAsset* FindAsset(const FGuid& AssetId) const;

	/** Currently selected asset for placement. */
	void SetSelectedPlacementAsset(const FGuid& AssetId) { SelectedPlacementAssetId = AssetId; }
	FGuid GetSelectedPlacementAsset() const { return SelectedPlacementAssetId; }

	FOnTerraDyneAssetLibraryChanged OnAssetLibraryChanged;

private:
	UPROPERTY()
	TArray<FTerraDyneImportedModelAsset> LoadedAssets;

	TMap<FGuid, int32> AssetIdToIndex;
	FGuid SelectedPlacementAssetId;

	void TryFindThumbnailForAsset(FTerraDyneImportedModelAsset& Asset);
};
