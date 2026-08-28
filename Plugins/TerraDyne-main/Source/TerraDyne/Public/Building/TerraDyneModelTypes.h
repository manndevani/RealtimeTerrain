// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "TerraDyneModelTypes.generated.h"

/**
 * One entry of the imported-model catalog, as saved.
 *
 * Only the source path travels, never the geometry: an imported model is built at runtime into an
 * FDynamicMesh3 that belongs to no package and cannot be serialised into a save game. A reload
 * re-imports from SourceFilePath, exactly the way FTerraDyneGeoTIFFImportState re-decodes its own
 * .tif rather than storing decoded heights.
 *
 * AssetId is saved alongside it so restored placements still resolve: re-importing mints a fresh
 * GUID otherwise, and every FTerraDynePlacedModelData referring to the old one would dangle.
 */
USTRUCT(BlueprintType)
struct FTerraDyneModelLibraryEntryData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FGuid AssetId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FString AssetName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FString SourceFilePath;
};

/**
 * One placed model instance, as saved.
 *
 * SourceFilePath is duplicated here rather than only being looked up through AssetId, so a
 * placement can still be restored when its catalog entry has gone missing -- the library entry was
 * removed before saving, or the save is being opened on a machine whose catalog is empty.
 */
USTRUCT(BlueprintType)
struct FTerraDynePlacedModelData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FGuid AssetId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FString SourceFilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FString CustomLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Model")
	bool bVisibleInScene = true;
};
