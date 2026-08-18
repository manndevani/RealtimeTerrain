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

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;

	ETerraDyneGizmoAxis CurrentHighlight = ETerraDyneGizmoAxis::None;
	int32 CurrentHighlightedPoint = INDEX_NONE;
};
