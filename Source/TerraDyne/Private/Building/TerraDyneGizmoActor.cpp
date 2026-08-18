// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/TerraDyneGizmoActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

ATerraDyneGizmoActor::ATerraDyneGizmoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EditorMaterials/TransformGizmoMaterial.TransformGizmoMaterial"));
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
	}
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/VREditor/TransformGizmo/TransformGizmoMaterial.TransformGizmoMaterial"));
	}
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	auto SetupMesh = [this, BaseMat](TObjectPtr<UStaticMeshComponent>& Comp, const TCHAR* Name, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale)
	{
		Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(SceneRoot);
		Comp->SetStaticMesh(Mesh);
		Comp->SetRelativeLocationAndRotation(Loc, Rot);
		Comp->SetRelativeScale3D(Scale);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Comp->SetCollisionObjectType(ECC_WorldDynamic);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		Comp->SetTranslucentSortPriority(10000);
		Comp->SetCastShadow(false);
	};

	const FRotator RotX = FRotationMatrix::MakeFromZ(FVector(1.0f, 0.0f, 0.0f)).Rotator();
	const FRotator RotY = FRotationMatrix::MakeFromZ(FVector(0.0f, 1.0f, 0.0f)).Rotator();
	const FRotator RotZ = FRotator::ZeroRotator;

	const FVector ShaftScale(0.06f, 0.06f, 2.0f);
	const FVector TipScale(0.22f, 0.22f, 0.5f);

	// X Axis (Red) - Points +X
	SetupMesh(MeshShaftX, TEXT("MeshShaftX"), CylinderMesh, FVector(100.0f, 0.0f, 0.0f), RotX, ShaftScale);
	SetupMesh(MeshTipX, TEXT("MeshTipX"), ConeMesh, FVector(200.0f, 0.0f, 0.0f), RotX, TipScale);

	// Y Axis (Green) - Points +Y
	SetupMesh(MeshShaftY, TEXT("MeshShaftY"), CylinderMesh, FVector(0.0f, 100.0f, 0.0f), RotY, ShaftScale);
	SetupMesh(MeshTipY, TEXT("MeshTipY"), ConeMesh, FVector(0.0f, 200.0f, 0.0f), RotY, TipScale);

	// Z Axis (Blue) - Points +Z
	SetupMesh(MeshShaftZ, TEXT("MeshShaftZ"), CylinderMesh, FVector(0.0f, 0.0f, 100.0f), RotZ, ShaftScale);
	SetupMesh(MeshTipZ, TEXT("MeshTipZ"), ConeMesh, FVector(0.0f, 0.0f, 200.0f), RotZ, TipScale);

	// Center XY Planar Disc (Yellow)
	SetupMesh(MeshCenterXY, TEXT("MeshCenterXY"), CylinderMesh, FVector(0.0f, 0.0f, 2.0f), FRotator::ZeroRotator, FVector(0.4f, 0.4f, 0.04f));

	auto SetMatColor = [](UMaterialInstanceDynamic* DMI, const FLinearColor& InColor)
	{
		if (!DMI) return;
		DMI->SetVectorParameterValue(TEXT("Color"), InColor);
		DMI->SetVectorParameterValue(TEXT("GizmoColor"), InColor);
		DMI->SetVectorParameterValue(TEXT("BaseColor"), InColor);
		DMI->SetVectorParameterValue(TEXT("Base Color"), InColor);
		DMI->SetVectorParameterValue(TEXT("EmissiveColor"), InColor * 2.5f);
		DMI->SetVectorParameterValue(TEXT("Emissive_Color"), InColor * 2.5f);
		DMI->SetVectorParameterValue(TEXT("Emissive"), InColor * 2.5f);
		DMI->SetVectorParameterValue(TEXT("Tint"), InColor);
	};

	if (BaseMat)
	{
		MatX = UMaterialInstanceDynamic::Create(BaseMat, this);
		MatY = UMaterialInstanceDynamic::Create(BaseMat, this);
		MatZ = UMaterialInstanceDynamic::Create(BaseMat, this);
		MatCenter = UMaterialInstanceDynamic::Create(BaseMat, this);

		SetMatColor(MatX, FLinearColor(1.0f, 0.08f, 0.08f, 1.0f));
		SetMatColor(MatY, FLinearColor(0.08f, 0.95f, 0.08f, 1.0f));
		SetMatColor(MatZ, FLinearColor(0.12f, 0.45f, 1.0f, 1.0f));
		SetMatColor(MatCenter, FLinearColor(1.0f, 0.9f, 0.08f, 1.0f));

		if (MeshShaftX && MatX) MeshShaftX->SetMaterial(0, MatX);
		if (MeshTipX && MatX) MeshTipX->SetMaterial(0, MatX);
		if (MeshShaftY && MatY) MeshShaftY->SetMaterial(0, MatY);
		if (MeshTipY && MatY) MeshTipY->SetMaterial(0, MatY);
		if (MeshShaftZ && MatZ) MeshShaftZ->SetMaterial(0, MatZ);
		if (MeshTipZ && MatZ) MeshTipZ->SetMaterial(0, MatZ);
		if (MeshCenterXY && MatCenter) MeshCenterXY->SetMaterial(0, MatCenter);

		MatSplinePoint = UMaterialInstanceDynamic::Create(BaseMat, this);
		MatSplinePointHover = UMaterialInstanceDynamic::Create(BaseMat, this);

		SetMatColor(MatSplinePoint, FLinearColor(0.1f, 0.95f, 0.95f, 1.0f));
		SetMatColor(MatSplinePointHover, FLinearColor(2.5f, 2.5f, 0.3f, 1.0f));
	}

	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
}

void ATerraDyneGizmoActor::UpdateSplinePointHandles(const TArray<FVector>& LocalSplinePoints, const FTransform& BuildingTransform)
{
	const int32 DesiredCount = LocalSplinePoints.Num();

	// Spawn or destroy components as needed
	while (SplinePointMeshHandles.Num() < DesiredCount)
	{
		const int32 Index = SplinePointMeshHandles.Num();
		const FName CompName = *FString::Printf(TEXT("SplinePointHandle_%d"), Index);
		UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this, CompName);
		if (NewComp)
		{
			NewComp->RegisterComponent();
			NewComp->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
			NewComp->SetStaticMesh(SphereMesh);
			NewComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			NewComp->SetCollisionObjectType(ECC_WorldDynamic);
			NewComp->SetCollisionResponseToAllChannels(ECR_Ignore);
			NewComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			NewComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			NewComp->SetTranslucentSortPriority(10000);
			NewComp->SetCastShadow(false);
			if (MatSplinePoint)
			{
				NewComp->SetMaterial(0, MatSplinePoint);
			}
			SplinePointMeshHandles.Add(NewComp);
		}
		else
		{
			break;
		}
	}

	while (SplinePointMeshHandles.Num() > DesiredCount)
	{
		UStaticMeshComponent* Comp = SplinePointMeshHandles.Pop();
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}

	// Position and scale handles
	for (int32 i = 0; i < DesiredCount; ++i)
	{
		if (UStaticMeshComponent* Comp = SplinePointMeshHandles[i])
		{
			const FVector WorldPt = BuildingTransform.TransformPosition(LocalSplinePoints[i]);
			Comp->SetWorldLocation(WorldPt);
			Comp->SetWorldScale3D(FVector(0.35f) * GetActorScale3D().X);
			Comp->SetMaterial(0, (i == CurrentHighlightedPoint) ? MatSplinePointHover : MatSplinePoint);
		}
	}
}

void ATerraDyneGizmoActor::SetHighlightedSplinePoint(int32 PointIndex)
{
	if (CurrentHighlightedPoint == PointIndex)
	{
		return;
	}
	CurrentHighlightedPoint = PointIndex;

	for (int32 i = 0; i < SplinePointMeshHandles.Num(); ++i)
	{
		if (UStaticMeshComponent* Comp = SplinePointMeshHandles[i])
		{
			Comp->SetMaterial(0, (i == PointIndex) ? MatSplinePointHover : MatSplinePoint);
		}
	}
}

int32 ATerraDyneGizmoActor::GetSplinePointIndexFromComponent(const UPrimitiveComponent* Component) const
{
	for (int32 i = 0; i < SplinePointMeshHandles.Num(); ++i)
	{
		if (SplinePointMeshHandles[i] == Component)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void ATerraDyneGizmoActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Dynamically scale gizmo with camera distance for uniform on-screen appearance
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const float Dist = FVector::Dist(Cam->GetCameraLocation(), GetActorLocation());
		const float Scale = FMath::Clamp(Dist * 0.0006f, 0.3f, 4.0f);
		SetActorScale3D(FVector(Scale));
	}
}

void ATerraDyneGizmoActor::SetHighlightedAxis(ETerraDyneGizmoAxis Axis)
{
	if (CurrentHighlight == Axis)
	{
		return;
	}
	CurrentHighlight = Axis;

	auto SetMatColor = [](UMaterialInstanceDynamic* DMI, const FLinearColor& InColor)
	{
		if (!DMI) return;
		DMI->SetVectorParameterValue(TEXT("Color"), InColor);
		DMI->SetVectorParameterValue(TEXT("GizmoColor"), InColor);
		DMI->SetVectorParameterValue(TEXT("BaseColor"), InColor);
		DMI->SetVectorParameterValue(TEXT("Base Color"), InColor);
		DMI->SetVectorParameterValue(TEXT("EmissiveColor"), InColor * 2.5f);
		DMI->SetVectorParameterValue(TEXT("Emissive_Color"), InColor * 2.5f);
		DMI->SetVectorParameterValue(TEXT("Emissive"), InColor * 2.5f);
		DMI->SetVectorParameterValue(TEXT("Tint"), InColor);
	};

	const FLinearColor NormalX(1.0f, 0.08f, 0.08f, 1.0f);
	const FLinearColor NormalY(0.08f, 0.95f, 0.08f, 1.0f);
	const FLinearColor NormalZ(0.12f, 0.45f, 1.0f, 1.0f);
	const FLinearColor NormalCenter(1.0f, 0.9f, 0.08f, 1.0f);
	const FLinearColor HighlightColor(2.5f, 2.5f, 0.4f, 1.0f);

	SetMatColor(MatX, Axis == ETerraDyneGizmoAxis::X ? HighlightColor : NormalX);
	SetMatColor(MatY, Axis == ETerraDyneGizmoAxis::Y ? HighlightColor : NormalY);
	SetMatColor(MatZ, Axis == ETerraDyneGizmoAxis::Z ? HighlightColor : NormalZ);
	SetMatColor(MatCenter, Axis == ETerraDyneGizmoAxis::CenterXY ? HighlightColor : NormalCenter);
}

ETerraDyneGizmoAxis ATerraDyneGizmoActor::GetAxisFromComponent(const UPrimitiveComponent* Component) const
{
	if (Component == MeshShaftX || Component == MeshTipX)
	{
		return ETerraDyneGizmoAxis::X;
	}
	if (Component == MeshShaftY || Component == MeshTipY)
	{
		return ETerraDyneGizmoAxis::Y;
	}
	if (Component == MeshShaftZ || Component == MeshTipZ)
	{
		return ETerraDyneGizmoAxis::Z;
	}
	if (Component == MeshCenterXY)
	{
		return ETerraDyneGizmoAxis::CenterXY;
	}
	return ETerraDyneGizmoAxis::None;
}
