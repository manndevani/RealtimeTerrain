// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoadCurveGraph.h"
#include "RoadStylePresets.generated.h"

class UMaterialInterface;

/** Mirrors RoadBuilder's ELaneMarkType (LaneMarkStyle.h). */
UENUM(BlueprintType)
enum class ERoadLaneMarkType : uint8
{
	Dash,
	Solid,
	DashDash,
	DashSolid,
	SolidDash,
	SolidSolid,
};

/**
 * A reusable lane-divider marking style, mirroring RoadBuilder's ULaneMarkStyle
 * (LaneMarkStyle.h). A UDataAsset rather than a plain UObject so content creators can create,
 * save, and pick one from the Content Browser with no custom factory needed.
 */
UCLASS(BlueprintType)
class REALTIMETERRAIN_API URoadLaneMarkStyle : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Style)
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Category = Style)
	ERoadLaneMarkType MarkType = ERoadLaneMarkType::Dash;

	UPROPERTY(EditAnywhere, Category = Style, meta = (ClampMin = "0.0"))
	double Width = 12.5;

	UPROPERTY(EditAnywhere, Category = Style, meta = (ClampMin = "0.0"))
	double Separation = 12.5;

	UPROPERTY(EditAnywhere, Category = Style, meta = (ClampMin = "1.0"))
	double DashLength = 150.0;

	UPROPERTY(EditAnywhere, Category = Style, meta = (ClampMin = "1.0"))
	double DashSpacing = 150.0;
};

/**
 * One vertex of a lane's cross-section, in the lane's own (Lateral, Vertical) plane -- the same
 * convention RoadMeshBuilder::FSection/FSectionVertex already uses, so a shape's points feed
 * straight into the existing sweep pipeline once a lane graph is wired into it. Mirrors
 * RoadBuilder's FLaneCrossSection, flattened to one material slot per point (matching
 * FSectionVertex::EdgeMaterialID, a property of the point describing the quad to the next one)
 * instead of FLaneCrossSection's separate Material-per-cross-section-run.
 */
USTRUCT(BlueprintType)
struct FRoadLaneShapePoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Point)
	double Lateral = 0.0;

	UPROPERTY(EditAnywhere, Category = Point)
	double Vertical = 0.0;

	/** Index into the owning URoadLaneShape's Materials array for the quad from this point to the next. */
	UPROPERTY(EditAnywhere, Category = Point, meta = (ClampMin = "0"))
	int32 MaterialSlot = 0;
};

/**
 * A reusable lane cross-section profile, mirroring RoadBuilder's ULaneShape (LaneShape.h) --
 * e.g. a flat driving lane, a cambered shoulder, a stepped sidewalk kerb.
 */
UCLASS(BlueprintType)
class REALTIMETERRAIN_API URoadLaneShape : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Shape)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	/** Ordered left-to-right across the lane's own width. Needs at least 2 points to sweep. */
	UPROPERTY(EditAnywhere, Category = Shape)
	TArray<FRoadLaneShapePoint> Points;

	UMaterialInterface* GetMaterial(int32 Slot) const
	{
		return Materials.IsValidIndex(Slot) ? Materials[Slot] : nullptr;
	}
};

/** One lane in a style preset's ordered left/right lists. Mirrors RoadBuilder's FRoadLaneStyle. */
USTRUCT(BlueprintType)
struct FRoadStyleLaneEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Lane, meta = (ClampMin = "0.0"))
	double Width = 350.0;

	UPROPERTY(EditAnywhere, Category = Lane)
	ERealtimeLaneType LaneType = ERealtimeLaneType::Driving;

	UPROPERTY(EditAnywhere, Category = Lane)
	TObjectPtr<URoadLaneShape> Shape;

	UPROPERTY(EditAnywhere, Category = Lane)
	TObjectPtr<URoadLaneMarkStyle> Marking;
};

/**
 * A named, reusable road cross-section preset -- e.g. "Street-Main-4-Lanes",
 * "Highway-Ramp-1-Lane" -- so content creators pick a style instead of tuning raw sliders per
 * road. Mirrors RoadBuilder's URoadStyle (RoadActor.h).
 */
UCLASS(BlueprintType)
class REALTIMETERRAIN_API URoadStylePreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Style)
	TArray<FRoadStyleLaneEntry> LeftLanes;

	UPROPERTY(EditAnywhere, Category = Style)
	TArray<FRoadStyleLaneEntry> RightLanes;

	UPROPERTY(EditAnywhere, Category = Style)
	TObjectPtr<URoadLaneMarkStyle> BaseCurveMark;

	/** Total width across both sides, ignoring markings/shape overhang. Ported from a flattened URoadStyle::NumDrivingLanes-style scan. */
	int32 NumDrivingLanes() const;

	double TotalWidth() const;
};
