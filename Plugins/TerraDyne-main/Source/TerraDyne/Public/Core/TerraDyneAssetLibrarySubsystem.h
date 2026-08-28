// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IO/TerraDyneModelImporter.h"
#include "Building/TerraDyneModelTypes.h"
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

	/**
	 * Reflected so the garbage collector can see the diffuse textures hanging off these materials.
	 * Without the UPROPERTY the collector never walks into the array and every imported texture is
	 * collected while the library still believes it holds one.
	 */
	UPROPERTY()
	TArray<FTerraDyneImportedMaterial> Materials;

	FDynamicMesh3 Mesh;
	FBox3d BoundingBox;
};

DECLARE_MULTICAST_DELEGATE(FOnTerraDyneAssetLibraryChanged);

/**
 * World subsystem managing imported 3D model assets, catalog registration, thumbnails, and placement palettes.
 *
 * Client-local by design. Imported geometry belongs to no package and cannot be replicated, so each
 * machine imports the file for itself; ATerraDynePlacedModelActor replicates the source path and
 * calls ResolveOrImport here to catch up.
 */
UCLASS()
class TERRADYNE_API UTerraDyneAssetLibrarySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Imports a 3D model file into the asset library.
	 *
	 * Re-importing a path already in the catalog returns the existing entry's id instead of minting
	 * a duplicate. Pass InAssetId to force a specific id -- how a save game restores its catalog
	 * with the ids its placements already refer to.
	 */
	FGuid ImportModelFromFile(const FString& FilePath, FString& OutError, const FGuid& InAssetId = FGuid());

	/** Batch imports all supported 3D models from a directory. */
	int32 ImportDirectory(const FString& DirectoryPath);

	/** Removes an asset from the library. */
	bool RemoveAsset(const FGuid& AssetId);

	/** Drops every catalog entry. Placed actors are untouched. */
	void ClearLibrary();

	/** Retrieves all currently loaded assets. */
	const TArray<FTerraDyneImportedModelAsset>& GetAssets() const { return LoadedAssets; }

	/** Finds an asset by its unique GUID. */
	const FTerraDyneImportedModelAsset* FindAsset(const FGuid& AssetId) const;

	/** Finds an asset by source file path, normalised and case-insensitive. */
	const FTerraDyneImportedModelAsset* FindAssetBySourcePath(const FString& FilePath) const;

	/**
	 * Returns the catalog entry for AssetId, importing SourceFilePath under that id first if this
	 * machine has never seen it. The path a replicated placement takes on a client that did not
	 * perform the original import. Null when the file is unavailable here.
	 */
	const FTerraDyneImportedModelAsset* ResolveOrImport(const FGuid& AssetId, const FString& SourceFilePath, FString& OutError);

	/** Currently selected asset for placement. */
	void SetSelectedPlacementAsset(const FGuid& AssetId) { SelectedPlacementAssetId = AssetId; }
	FGuid GetSelectedPlacementAsset() const { return SelectedPlacementAssetId; }

	//--- Persistence ---

	/** The catalog as save-game data: ids, names and source paths, never geometry. */
	TArray<FTerraDyneModelLibraryEntryData> ExportLibrary() const;

	/** Replaces the catalog by re-importing each entry's source file under its saved id. Returns how many resolved. */
	int32 RestoreLibrary(const TArray<FTerraDyneModelLibraryEntryData>& Entries);

	FOnTerraDyneAssetLibraryChanged OnAssetLibraryChanged;

private:
	UPROPERTY()
	TArray<FTerraDyneImportedModelAsset> LoadedAssets;

	TMap<FGuid, int32> AssetIdToIndex;
	FGuid SelectedPlacementAssetId;

	void TryFindThumbnailForAsset(FTerraDyneImportedModelAsset& Asset);
	void RebuildAssetIndex();

	/** Full, lower-cased path, so the same file reached by two spellings dedupes to one entry. */
	static FString NormalizeSourcePath(const FString& FilePath);
};
