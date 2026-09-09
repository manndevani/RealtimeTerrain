// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GRTBuildingKit.generated.h"

class UStaticMesh;

/** One wall panel width available to the greedy segment filler. */
USTRUCT(BlueprintType)
struct FGRTWallPanelEntry
{
	GENERATED_BODY()

	/** Panel width along the wall run, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building", meta = (ClampMin = "1"))
	int32 WidthUnits = 400;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/**
 * One square slab size available to the subdividing floor filler.
 *
 * Sizes must form a halving chain (400 -> 200 -> 100) so a cell that does not fit divides evenly
 * into cells of the next size down.
 */
USTRUCT(BlueprintType)
struct FGRTFloorTileEntry
{
	GENERATED_BODY()

	/** Tile edge length in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building", meta = (ClampMin = "1"))
	int32 SizeUnits = 400;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/**
 * A modular mesh set plus the pivot convention needed to place it.
 *
 * The class defaults are populated with the SD_Art Industrial Architecture kit, so a building
 * actor with no Kit assigned still generates. Author a DataAsset from this class to make a
 * second style.
 *
 * PIVOT CONVENTION (measured from the SD_Art meshes, not assumed from filenames):
 *   Wall panels  — pivot at the bottom corner, run along local -Y, rise along +Z, thickness in X.
 *   Floor tiles  — pivot at a corner, occupy +X / -Y, top face at Z = 0.
 *   Corner posts — pivot at the base, rise along +Z.
 *
 * SegmentYawOffset exists because of the wall run direction: a yaw of theta maps local -Y onto
 * world (sin theta, -cos theta). To point local -Y along a segment direction D we need
 * theta = atan2(D.X, -D.Y), which is exactly atan2(D.Y, D.X) + 90 degrees. A kit whose panels
 * run along +X instead would set this to 0.
 */
UCLASS(BlueprintType)
class GRT_API UGRTBuildingKit : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGRTBuildingKit();

	/** Smallest placement quantum along a wall. Footprint grid snap uses this too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Grid", meta = (ClampMin = "1"))
	float ModuleWidth = 400.0f;

	/** One storey. Every panel in the SD_Art kit is exactly this tall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Grid", meta = (ClampMin = "1"))
	float FloorHeight = 400.0f;

	/** Yaw added to a segment's direction angle to align the panel run. See class comment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Grid")
	float SegmentYawOffset = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Meshes")
	TArray<FGRTWallPanelEntry> WallPanels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Meshes")
	TSoftObjectPtr<UStaticMesh> WallPanelDoor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Meshes")
	TSoftObjectPtr<UStaticMesh> CornerPost;

	/** Slab sizes for floors. Largest first; each must halve into the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Meshes")
	TArray<FGRTFloorTileEntry> FloorTiles;

	/** Slab sizes for the roof. Falls back to FloorTiles when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Building|Meshes")
	TArray<FGRTFloorTileEntry> RoofTiles;

	/** Panel widths, largest first — the order the greedy filler wants them in. */
	TArray<int32> GetPanelWidthsDescending() const;

	/** Loads and returns the panel of exactly this width, or nullptr. */
	UStaticMesh* ResolvePanelOfWidth(int32 WidthUnits) const;

	/** Narrowest available panel width, or 0 if the kit has none. */
	int32 GetSmallestPanelWidth() const;

	/** Floor slab sizes sorted largest first, entries with no mesh dropped. */
	TArray<FGRTFloorTileEntry> GetFloorTilesDescending() const;

	/** Roof slab sizes sorted largest first, falling back to the floor set. */
	TArray<FGRTFloorTileEntry> GetRoofTilesDescending() const;

	/** True if there is at least one wall panel with a loadable mesh. */
	bool IsUsable() const;
};
