// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TerraDyneGizmoActor.generated.h"

UENUM(BlueprintType)
enum class ETerraDyneGizmoAxis : uint8
{
	None,
	X,
	Y,
	Z,
	CenterXY
};

UCLASS()
class TERRADYNE_API ATerraDyneGizmoActor : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneGizmoActor();

	virtual void Tick(float DeltaTime) override;
	virtual void PostInitializeComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	void SetHighlightedAxis(ETerraDyneGizmoAxis Axis);
	ETerraDyneGizmoAxis GetAxisFromComponent(const UPrimitiveComponent* Component) const;

	void UpdateSplinePointHandles(const TArray<FVector>& LocalSplinePoints, const FTransform& BuildingTransform);
	void SetHighlightedSplinePoint(int32 PointIndex);
	int32 GetSplinePointIndexFromComponent(const UPrimitiveComponent* Component) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshShaftX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshTipX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshShaftY;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshTipY;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshShaftZ;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshTipZ;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<UStaticMeshComponent> MeshCenterXY;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TArray<TObjectPtr<UStaticMeshComponent>> SplinePointMeshHandles;

private:
	/**
	 * Base material the per-axis MIDs are built from, resolved once in the constructor.
	 *
	 * Deliberately NOT Transient. The cooker only follows hard references reachable from a
	 * serialized property of the CDO — the axis meshes cook because a default subobject's
	 * StaticMesh property holds them, but a material reachable only through a runtime MID is
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
	TObjectPtr<UMaterialInstanceDynamic> MatX;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatY;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatZ;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatCenter;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatSplinePoint;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MatSplinePointHover;

	/** Spline-point handle mesh. Non-Transient for the same cooking reason as GizmoBaseMaterial. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	ETerraDyneGizmoAxis CurrentHighlight = ETerraDyneGizmoAxis::None;
	int32 CurrentHighlightedPoint = INDEX_NONE;
};
