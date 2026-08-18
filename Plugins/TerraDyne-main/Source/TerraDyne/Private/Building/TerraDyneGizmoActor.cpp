// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/TerraDyneGizmoActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "TerraDyneModule.h"
#include "UObject/ConstructorHelpers.h"

ATerraDyneGizmoActor::ATerraDyneGizmoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// FObjectFinder rather than LoadObject: it is the sanctioned constructor-time form, it is
	// cached across CDO reconstruction, and it reports a bad path loudly at load instead of
	// silently handing back null.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// This gizmo is a RUNTIME tool, so its material has to survive cooking. The previous first
	// choice was /Engine/EditorMaterials/TransformGizmoMaterial, with /Engine/VREditor/... behind
	// it — both editor-only trees that are never cooked into a packaged game. In the editor the
	// first one always won, so the runtime-safe path below was never actually exercised.
	//
	// GizmoMaterial lives in /Engine/EngineMaterials (cooked runtime content) and is the engine's
	// own gizmo material, exposing a GizmoColor parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GizmoMatFinder(TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMesh* CylinderMesh = CylinderFinder.Object;
	UStaticMesh* ConeMesh = ConeFinder.Object;
	SphereMesh = SphereFinder.Object;
	GizmoBaseMaterial = GizmoMatFinder.Object ? GizmoMatFinder.Object : BasicMatFinder.Object;

	auto SetupMesh = [this](TObjectPtr<UStaticMeshComponent>& Comp, const TCHAR* Name, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale)
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

}

void ATerraDyneGizmoActor::ApplyGizmoColor(UMaterialInstanceDynamic* DMI, const FLinearColor& InColor)
{
	if (!DMI)
	{
		return;
	}

	// Two base materials are possible and they name their colour differently: GizmoMaterial exposes
	// GizmoColor, BasicShapeMaterial exposes Color/BaseColor. Setting a parameter a material does
	// not have is a no-op, so writing both covers either without branching on which one loaded.
	DMI->SetVectorParameterValue(TEXT("GizmoColor"), InColor);
	DMI->SetVectorParameterValue(TEXT("Color"), InColor);
	DMI->SetVectorParameterValue(TEXT("BaseColor"), InColor);
	DMI->SetVectorParameterValue(TEXT("EmissiveColor"), InColor * 2.5f);
}

void ATerraDyneGizmoActor::EnsureDynamicMaterials()
{
	if (MatX)
	{
		return;
	}

	if (!GizmoBaseMaterial)
	{
		UE_LOG(LogTerraDyne, Warning,
			TEXT("Gizmo has no base material — handles will render with the default material."));
		return;
	}

	MatX = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatY = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatZ = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatCenter = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatSplinePoint = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatSplinePointHover = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);

	ApplyGizmoColor(MatX, FLinearColor(1.0f, 0.08f, 0.08f, 1.0f));
	ApplyGizmoColor(MatY, FLinearColor(0.08f, 0.95f, 0.08f, 1.0f));
	ApplyGizmoColor(MatZ, FLinearColor(0.12f, 0.45f, 1.0f, 1.0f));
	ApplyGizmoColor(MatCenter, FLinearColor(1.0f, 0.9f, 0.08f, 1.0f));
	ApplyGizmoColor(MatSplinePoint, FLinearColor(0.1f, 0.95f, 0.95f, 1.0f));
	ApplyGizmoColor(MatSplinePointHover, FLinearColor(2.5f, 2.5f, 0.3f, 1.0f));

	if (MeshShaftX) MeshShaftX->SetMaterial(0, MatX);
	if (MeshTipX) MeshTipX->SetMaterial(0, MatX);
	if (MeshShaftY) MeshShaftY->SetMaterial(0, MatY);
	if (MeshTipY) MeshTipY->SetMaterial(0, MatY);
	if (MeshShaftZ) MeshShaftZ->SetMaterial(0, MatZ);
	if (MeshTipZ) MeshTipZ->SetMaterial(0, MatZ);
	if (MeshCenterXY) MeshCenterXY->SetMaterial(0, MatCenter);
}

/**
 * MIDs are built per spawned gizmo, and never in the constructor.
 *
 * A constructor runs against the CDO, so a MID created there is owned by the CDO and every
 * spawned gizmo inherits the same pointer — highlighting an axis on one would recolour all of
 * them. It only looked correct because there is rarely more than one gizmo alive at a time.
 */
void ATerraDyneGizmoActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureDynamicMaterials();
}

void ATerraDyneGizmoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureDynamicMaterials();
}

void ATerraDyneGizmoActor::UpdateSplinePointHandles(const TArray<FVector>& LocalSplinePoints, const FTransform& BuildingTransform)
{
	EnsureDynamicMaterials();

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
	EnsureDynamicMaterials();

	if (CurrentHighlight == Axis)
	{
		return;
	}
	CurrentHighlight = Axis;

	const FLinearColor NormalX(1.0f, 0.08f, 0.08f, 1.0f);
	const FLinearColor NormalY(0.08f, 0.95f, 0.08f, 1.0f);
	const FLinearColor NormalZ(0.12f, 0.45f, 1.0f, 1.0f);
	const FLinearColor NormalCenter(1.0f, 0.9f, 0.08f, 1.0f);
	const FLinearColor HighlightColor(2.5f, 2.5f, 0.4f, 1.0f);

	ApplyGizmoColor(MatX, Axis == ETerraDyneGizmoAxis::X ? HighlightColor : NormalX);
	ApplyGizmoColor(MatY, Axis == ETerraDyneGizmoAxis::Y ? HighlightColor : NormalY);
	ApplyGizmoColor(MatZ, Axis == ETerraDyneGizmoAxis::Z ? HighlightColor : NormalZ);
	ApplyGizmoColor(MatCenter, Axis == ETerraDyneGizmoAxis::CenterXY ? HighlightColor : NormalCenter);
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
