// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GRTGizmoActor.generated.h"

/**
 * Visual handle spheres for editing a building's footprint spline -- one per control point,
 * spawned/repositioned on demand as the footprint changes.
 *
 * This is NOT a transform gizmo, despite the name: the runtime translate/rotate/scale gizmo used
 * to move/rotate/resize a whole building (or a road control point) is a separate, generic,
 * reusable system now (Plugins/GRT/Source/GRT/Public/Gizmo/ --
 * UGizmoManipulationSubsystem + ARuntimeGizmoActor). This class used to own BOTH responsibilities;
 * once the axis/plane/rotate/scale handle set moved to that new system, only the footprint-vertex
 * handles were left here. The name is a historical artifact kept to limit the size of that change,
 * not a description of current scope.
 */
UCLASS()
class GRT_API AGRTGizmoActor : public AActor
{
	GENERATED_BODY()

public:
	AGRTGizmoActor();

	virtual void Tick(float DeltaTime) override;
	virtual void PostInitializeComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	void UpdateSplinePointHandles(const TArray<FVector>& LocalSplinePoints, const FTransform& BuildingTransform);
	void SetHighlightedSplinePoint(int32 PointIndex);
	int32 GetSplinePointIndexFromComponent(const UPrimitiveComponent* Component) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TArray<TObjectPtr<UStaticMeshComponent>> SplinePointMeshHandles;

private:
	/**
	 * Base material the spline-point-handle MIDs are built from, resolved once in the constructor.
	 *
	 * Deliberately NOT Transient. The cooker only follows hard references reachable from a
	 * serialized property of the CDO — a material reachable only through a runtime MID is
	 * invisible to it and would be missing from a packaged build.
	 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GizmoBaseMaterial;

	/**
	 * Builds this instance's MIDs and applies the resting colours, once. Idempotent.
	 *
	 * Called from several entry points on purpose rather than from one lifecycle hook.
	 * PostInitializeComponents alone is not enough: AActor::PostActorConstruction only calls it
	 * when the world has already initialised its actors, so a gizmo spawned into an editor world
	 * that has not begun play would never get its materials.
	 */
	void EnsureDynamicMaterials();

	/** Writes a colour across the parameter names used by both possible base materials. */
	static void ApplyGizmoColor(UMaterialInstanceDynamic* DMI, const FLinearColor& InColor);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatSplinePoint;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatSplinePointHover;

	/** Spline-point handle mesh. Non-Transient for the same cooking reason as GizmoBaseMaterial. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	int32 CurrentHighlightedPoint = INDEX_NONE;
};
