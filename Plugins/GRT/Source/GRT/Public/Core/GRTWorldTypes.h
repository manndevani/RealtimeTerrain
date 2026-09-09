// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "GRTWorldTypes.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class EGRTPopulationKind : uint8
{
	Prop UMETA(DisplayName = "Prop"),
	Harvestable UMETA(DisplayName = "Harvestable"),
	Destroyable UMETA(DisplayName = "Destroyable"),
	RuntimePlaced UMETA(DisplayName = "Runtime Placed"),
	ProceduralSpawn UMETA(DisplayName = "Procedural Spawn")
};

UENUM(BlueprintType)
enum class EGRTPopulationState : uint8
{
	Active UMETA(DisplayName = "Active"),
	Harvested UMETA(DisplayName = "Harvested"),
	Destroyed UMETA(DisplayName = "Destroyed"),
	Regrowing UMETA(DisplayName = "Regrowing")
};

UENUM(BlueprintType)
enum class EGRTBuildPermission : uint8
{
	Inherit UMETA(DisplayName = "Inherit"),
	Allowed UMETA(DisplayName = "Allowed"),
	Blocked UMETA(DisplayName = "Blocked")
};

USTRUCT(BlueprintType)
struct FGRTPopulationDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	EGRTPopulationKind Kind = EGRTPopulationKind::Prop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName TypeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bReplicates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bAttachToChunk = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bSnapToTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float TerrainOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bAllowRegrowth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float RegrowthDelaySeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName BiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	TArray<FName> Tags;
};

USTRUCT(BlueprintType)
struct FGRTPersistentPopulationEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FGuid PopulationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FGRTPopulationDescriptor Descriptor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	EGRTPopulationState State = EGRTPopulationState::Active;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	float RemainingRegrowthTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bSpawnedFromRule = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName SourceRuleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName CachedBiomeTag = NAME_None;
};

USTRUCT(BlueprintType)
struct FGRTPopulationSpawnRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName RuleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FGRTPopulationDescriptor Descriptor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bAllowOnAuthoredChunks = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bAllowOnProceduralChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName RequiredBiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 MinDistanceFromAuthored = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 MaxDistanceFromAuthored = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 MinInstancesPerChunk = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 MaxInstancesPerChunk = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float ChunkPadding = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float MinSpacing = 300.0f;
};

USTRUCT(BlueprintType)
struct FGRTBiomeOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName BiomeTag = NAME_None;



	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bApplyToAuthoredChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bApplyToProceduralChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float ProceduralNoiseMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float ProceduralNoiseMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 Priority = 0;
};

USTRUCT(BlueprintType)
struct FGRTProceduralWorldSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 WorldSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bEnableSeededOutskirts = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bEnableInfiniteEdgeGrowth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 ProceduralOutskirtsExtent = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bGeneratePopulationFromRules = true;
};

USTRUCT(BlueprintType)
struct FGRTProceduralChunkState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FIntPoint Coordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ChunkSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bIsAuthoredChunk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bGeneratedOutskirtsChunk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bPopulationGenerated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 DistanceFromAuthored = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName PrimaryBiomeTag = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TArray<FName> OverlayBiomeTags;
};

USTRUCT(BlueprintType)
struct FGRTAISpawnZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FBox LocalBounds = FBox(FVector(-5000.0f, -5000.0f, -500.0f), FVector(5000.0f, 5000.0f, 5000.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	bool bProceduralOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName RequiredBiomeTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	int32 MaxConcurrentSpawns = 6;
};

USTRUCT(BlueprintType)
struct FGRTBuildPermissionZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FBox LocalBounds = FBox(FVector(-5000.0f, -5000.0f, -500.0f), FVector(5000.0f, 5000.0f, 5000.0f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	EGRTBuildPermission Permission = EGRTBuildPermission::Inherit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	FString Reason;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT")
	float MaxSlopeDegrees = 45.0f;
};

USTRUCT(BlueprintType)
struct FGRTPCGPoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName BiomeTag = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName SourceId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	EGRTPopulationKind PopulationKind = EGRTPopulationKind::Prop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	float Density = 1.0f;
};

USTRUCT(BlueprintType)
struct FGRTTerrainChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FBox WorldBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	bool bAffectsNavigation = false;
};

USTRUCT(BlueprintType)
struct FGRTFoliageChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	int32 ImportedFoliageInstanceCount = 0;
};

USTRUCT(BlueprintType)
struct FGRTPopulationChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FGuid PopulationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName TypeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	EGRTPopulationKind Kind = EGRTPopulationKind::Prop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	EGRTPopulationState State = EGRTPopulationState::Active;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	FName Reason = NAME_None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGRTTerrainChangedDelegate, const FGRTTerrainChangeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGRTFoliageChangedDelegate, const FGRTFoliageChangeEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGRTPopulationChangedDelegate, const FGRTPopulationChangeEvent&, Event);

/**
 * Fired once, the moment an undecided world's Sandbox/Import choice actually resolves --
 * AGRTManager::MarkWorldModeChosen() clearing bAwaitingWorldModeChoice from true to false.
 * Anything that wants to build world content but must not jump ahead of the player's choice (the
 * showcase/demo sequence, notably) waits on this instead of either racing BeginPlay order against
 * the manager or, worse, calling MarkWorldModeChosen() itself just to unblock its own startup --
 * the latter silently resolves the choice for the player before they ever see it.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGRTWorldModeChosenDelegate);
