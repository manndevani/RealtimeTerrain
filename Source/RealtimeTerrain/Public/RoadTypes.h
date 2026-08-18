// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoadTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;
class ARealtimeRoadJunctionActor;

UENUM(BlueprintType)
enum class ERoadType : uint8
{
	RoughRoad UMETA(DisplayName = "Rough Road / Dirt Trail"),
	PavedRoad UMETA(DisplayName = "Paved Road (Curbs + Sidewalks)")
};

UENUM(BlueprintType)
enum class ERoadJunctionType : uint8
{
	T_Junction UMETA(DisplayName = "3-Way T-Junction"),
	Cross_4Way UMETA(DisplayName = "4-Way Crossroads"),
	Y_Fork UMETA(DisplayName = "Y-Fork (3-Way Branch)"),
	Custom_N_Way UMETA(DisplayName = "Custom N-Way Junction")
};

/**
 * Shape of the road slab in cross-section, plus the tessellation and UV settings that govern how
 * it is swept along the spline.
 *
 * Applies to both road types. Rough roads use the width, thickness, step length and UV scale and
 * ignore the curb and footpath fields.
 */
USTRUCT(BlueprintType)
struct FRoadCrossSectionSettings
{
	GENERATED_BODY()

	/** Width of the drivable surface in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (ClampMin = "200.0", UIMin = "200.0"))
	float RoadWidth = 800.0f;

	/** Vertical thickness of the slab. The road is a closed solid, so this is visible from the side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (ClampMin = "2.0"))
	float RoadThickness = 25.0f;

	/**
	 * Crown of the drive surface as a percentage of half-width, shedding water to both edges.
	 * Zero gives a dead-flat surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (ClampMin = "0.0", UIMax = "6.0"))
	float CamberPercent = 2.0f;

	/** Include raised concrete curbs. Paved roads only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection")
	bool bHasCurbs = true;

	/** Width of curb stone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (EditCondition = "bHasCurbs", ClampMin = "5.0"))
	float CurbWidth = 25.0f;

	/** Height of the curb top above the road surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (EditCondition = "bHasCurbs", ClampMin = "2.0"))
	float CurbHeight = 18.0f;

	/** Include pedestrian footpaths outboard of the curbs. Paved roads only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection")
	bool bHasFootpaths = true;

	/** Width of sidewalk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (EditCondition = "bHasFootpaths", ClampMin = "50.0"))
	float FootpathWidth = 160.0f;

	/**
	 * Height of the sidewalk above the road surface. Sitting a little below the curb top leaves a
	 * visible lip; matching CurbHeight makes the two flush.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|CrossSection", meta = (EditCondition = "bHasFootpaths", ClampMin = "2.0"))
	float FootpathHeight = 15.0f;

	/**
	 * Spacing in cm between swept cross-sections. Also the resolution at which the road samples
	 * terrain, so lowering it makes the road hug the ground more closely at the cost of triangles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Generation", meta = (ClampMin = "40.0", ClampMax = "600.0"))
	float SegmentStepLength = 150.0f;

	/** World units to UV units. UVs are world-scaled on both axes, so texel density stays uniform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Generation", meta = (ClampMin = "0.00001"))
	float UVTilingScale = 0.002f;
};

/** Rock and debris scattered along the margins of a rough road. */
USTRUCT(BlueprintType)
struct FRoadScatterSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter")
	bool bEnableScatter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter")
	TSoftObjectPtr<UStaticMesh> ScatterMesh;

	/** Instances per 10 metres of road, per side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float DensityPer10Meters = 3.0f;

	/**
	 * Nearest a scattered instance may sit to the road centreline. Kept clear of the default
	 * half-width so debris does not spawn on the running surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter", meta = (ClampMin = "0.0"))
	float MinMarginOffset = 560.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter", meta = (ClampMin = "0.0"))
	float MaxMarginOffset = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter", meta = (ClampMin = "0.01"))
	float MinScale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter", meta = (ClampMin = "0.01"))
	float MaxScale = 1.3f;

	/** Random seed for deterministic placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter")
	int32 RandomSeed = 42;

	/** Sink instances into the ground by this much so they do not appear to float. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Scatter", meta = (ClampMin = "0.0"))
	float EmbedDepth = 12.0f;
};

/**
 * A stretch of a road that a junction occupies.
 *
 * A road keeps one continuous spline through a junction and simply omits these stations when it
 * builds its mesh, then pins the two exposed ends to the junction's leg sockets. Cutting rather
 * than splitting into separate actors means the road keeps its identity, and removing a junction
 * is a matter of dropping the cut and rebuilding instead of stitching actors back together.
 */
USTRUCT(BlueprintType)
struct FRoadJunctionCut
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction")
	TObjectPtr<ARealtimeRoadJunctionActor> Junction = nullptr;

	/** Distance along the spline where the road enters the junction footprint, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction")
	float EntryDistance = 0.0f;

	/** Distance along the spline where the road leaves it again, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction")
	float ExitDistance = 0.0f;

	/** Leg the approaching stretch of road meets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction")
	int32 EntryLegIndex = INDEX_NONE;

	/** Leg the departing stretch meets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Junction")
	int32 ExitLegIndex = INDEX_NONE;
};

/** One road arm radiating from a junction. */
USTRUCT(BlueprintType)
struct FRoadJunctionLeg
{
	GENERATED_BODY()

	/** Heading of the leg in degrees. 0 is +X, 90 is +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction")
	float AngleDegrees = 0.0f;

	/** Distance from the junction centre to the leg's connection socket, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction", meta = (ClampMin = "100.0"))
	float ArmLength = 500.0f;

	/** Width for this leg. Zero or less falls back to the junction's RoadWidth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction", meta = (ClampMin = "0.0"))
	float CustomRoadWidth = 0.0f;
};
