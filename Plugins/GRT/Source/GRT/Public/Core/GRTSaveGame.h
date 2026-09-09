// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Core/GRTWorldTypes.h"
#include "Building/GRTBuildingTypes.h"
#include "Building/GRTModelTypes.h"
#include "GRTSaveGame.generated.h"

USTRUCT(BlueprintType)
struct FGRTChunkData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FIntPoint Coordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 Resolution = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	float ZScale = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<float> HeightData;

	// Separate height layers (absent on old saves → graceful fallback to HeightData)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<float> BaseData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<float> SculptData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<float> DetailData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bTransferredFoliageFollowsTerrain = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FString> FoliageStaticMeshPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<int32> FoliageMaterialCounts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FString> FoliageOverrideMaterialPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<int32> FoliageDefinitionIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FTransform> FoliageInstanceLocalTransforms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<float> FoliageInstanceTerrainOffsets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FString> ActorFoliageClassPaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<uint8> ActorFoliageAttachFlags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<int32> ActorFoliageDefinitionIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FTransform> ActorFoliageInstanceLocalTransforms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<float> ActorFoliageInstanceTerrainOffsets;
};

USTRUCT(BlueprintType)
struct FGRTLandscapeLayerMapping
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName SourceLayerName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 GRTWeightLayerIndex = INDEX_NONE;
};

/**
 * Where a world's terrain came from. Procedural is the default; the other two both set
 * AGRTManager::ImportedChunkCoords (which every downstream biome/population/outskirts-growth
 * decision already keys off, source-agnostically) but differ in what state accompanies them --
 * FGRTLandscapeMigrationState for ImportedLandscape, FGRTGeoTIFFImportState for
 * ImportedGeoTIFF.
 */
UENUM(BlueprintType)
enum class EGRTChunkOrigin : uint8
{
	Procedural,
	ImportedLandscape,
	ImportedGeoTIFF
};

/** Metadata for a world imported from a georeferenced GeoTIFF, mirroring FGRTLandscapeMigrationState's role for the Landscape-import case. */
USTRUCT(BlueprintType)
struct FGRTGeoTIFFImportState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bWasImportedFromGeoTIFF = false;

	/** Absolute path to the source .tif/.tiff, kept so a reload can re-decode it (see ApplyLoadedSaveGame). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString SourceFilePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FVector2D WorldFootprintMin = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FVector2D WorldFootprintMax = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	float ResolvedZScale = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString ImportedAtIso8601;
};

USTRUCT(BlueprintType)
struct FGRTLandscapeMigrationState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bWasImportedFromLandscape = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bImportedPaintLayers = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bSourceLandscapeHidden = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bAdoptedLandscapeMaterial = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bTransferredPlacedFoliage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bTransferredFoliageFollowsTerrain = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString SourceLandscapeName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString SourceLandscapePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString SourceLandscapeMaterialPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FVector SourceLandscapeLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FVector SourceLandscapeScale = FVector::OneVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FVector RuntimeManagerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	float ImportedChunkSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ImportedResolution = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ImportedComponentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ImportedWeightLayerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ImportedFoliageDefinitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ImportedFoliageInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString ImportedAtIso8601;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTLandscapeLayerMapping> LayerMappings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FString> UnmappedLayerNames;
};

UCLASS()
class GRT_API UGRTSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FDateTime Timestamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FVector ManagerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	float SavedGlobalChunkSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FString MasterMaterialPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FGRTLandscapeMigrationState LandscapeMigration;

	/**
	 * Defaults to Procedural on old saves (the field simply did not exist, and UE zero-initializes
	 * new USTRUCT/enum fields on load) -- ApplyLoadedSaveGame infers ImportedLandscape from
	 * LandscapeMigration.bWasImportedFromLandscape in that case, the same legacy-fallback idiom
	 * already used there for AuthoredChunkCoords.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	EGRTChunkOrigin WorldOriginKind = EGRTChunkOrigin::Procedural;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FGRTGeoTIFFImportState GeoTIFFImport;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FGRTProceduralWorldSettings ProceduralWorldSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTBiomeOverlay> BiomeOverlays;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTPopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTAISpawnZone> AISpawnZones;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTBuildPermissionZone> BuildPermissionZones;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTProceduralChunkState> ProceduralChunks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTPersistentPopulationEntry> PersistentPopulationEntries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FIntPoint> AuthoredChunkCoords;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTBuildingData> Buildings;

	/**
	 * The imported-model catalog and every placed instance. Both absent on saves written before
	 * 3D model import existed, which load as empty arrays and restore nothing -- the same
	 * graceful-degradation the layered chunk buffers already rely on.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTModelLibraryEntryData> ModelLibrary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FGRTPlacedModelData> PlacedModels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TMap<FIntPoint, FGRTChunkData> Chunks;
};
