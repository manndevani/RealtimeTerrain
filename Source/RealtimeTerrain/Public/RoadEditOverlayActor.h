// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadEditOverlayActor.generated.h"

class ARealtimeRoadSplineActor;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Visual overlay for road editing: the spline route drawn as a line, and a grabbable handle on
 * every control point.
 *
 * Deliberately built from real static meshes and a cooked engine material rather than debug draw.
 * DrawDebug calls are compiled out of Shipping, and this is a runtime authoring tool, so the
 * overlay has to survive a packaged build the same way the buildings gizmo does.
 *
 * Transient by construction — it is a view of a road, never saved with the level.
 */
UCLASS(NotBlueprintable, NotPlaceable, ClassGroup=(Roads))
class REALTIMETERRAIN_API ARoadEditOverlayActor : public AActor
{
	GENERATED_BODY()

public:
	ARoadEditOverlayActor();

	virtual void PostInitializeComponents() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlay")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Cylinders laid end to end along the sampled route. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlay")
	TObjectPtr<UInstancedStaticMeshComponent> RouteSegments;

	/** One sphere per control point. Blocks the visibility channel so a cursor trace can hit it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlay")
	TArray<TObjectPtr<UStaticMeshComponent>> PointHandles;

	/** Spacing in cm at which the route line is sampled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay", meta = (ClampMin = "20.0"))
	float RouteSampleSpacing = 120.0f;

	/** Diameter of the route line in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay", meta = (ClampMin = "1.0"))
	float RouteThickness = 22.0f;

	/** Diameter of a control point handle in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay", meta = (ClampMin = "1.0"))
	float HandleDiameter = 90.0f;

	/** Lift above the route so the overlay reads clearly against the road surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	float VerticalOffset = 40.0f;

	/** Rebuilds the overlay for a road. Passing null hides everything. */
	void ShowRoad(const ARealtimeRoadSplineActor* Road);

	/** Handle under the cursor, tinted differently from the rest. */
	void SetHighlightedPoint(int32 PointIndex);

	/** Handle the tool is currently anchored to. */
	void SetSelectedPoint(int32 PointIndex);

	/** Which control point a hit component belongs to, or INDEX_NONE if it is not a handle. */
	int32 GetPointIndexFromComponent(const UPrimitiveComponent* Component) const;

private:
	/** Builds the MIDs and applies resting colours. Idempotent; safe to call from anywhere. */
	void EnsureDynamicMaterials();

	/** Re-applies handle tints from the current highlight and selection. */
	void RefreshHandleMaterials();

	/**
	 * Meshes and base material are plain UPROPERTYs rather than Transient on purpose: the cooker
	 * only follows hard references reachable from a serialized property, so a material reached
	 * only through a runtime MID would be missing from a packaged build.
	 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverlayBaseMaterial;

	/**
	 * Translucent material with Disable Depth Test, so selected handles and the route line render
	 * on top of roads, terrain and walls. Lives in /Game/Materials/M_OverlayForeground.
	 * Falls back to OverlayBaseMaterial when missing so nothing breaks before the asset exists.
	 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverlayForegroundMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatRoute;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatHandle;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatHandleHover;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatHandleSelected;

	int32 HighlightedPoint = INDEX_NONE;
	int32 SelectedPoint = INDEX_NONE;
};
