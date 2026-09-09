// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/GRTGizmoActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GRTModule.h"
#include "UObject/ConstructorHelpers.h"

AGRTGizmoActor::AGRTGizmoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// FObjectFinder rather than LoadObject: it is the sanctioned constructor-time form, it is
	// cached across CDO reconstruction, and it reports a bad path loudly at load instead of
	// silently handing back null.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// This is a RUNTIME visual, so its material has to survive cooking. The previous first choice
	// was /Engine/EditorMaterials/TransformGizmoMaterial, with /Engine/VREditor/... behind it --
	// both editor-only trees that are never cooked into a packaged game. In the editor the first
	// one always won, so the runtime-safe path below was never actually exercised.
	//
	// GizmoMaterial lives in /Engine/EngineMaterials (cooked runtime content) and is the engine's
	// own gizmo material, exposing a GizmoColor parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GizmoMatFinder(TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForegroundMatFinder(TEXT("/Game/Mytaverse/TransformGizmo/Materials/M_GizmoAxis.M_GizmoAxis"));

	SphereMesh = SphereFinder.Object;
	GizmoBaseMaterial = ForegroundMatFinder.Object
		? ForegroundMatFinder.Object
		: (GizmoMatFinder.Object ? GizmoMatFinder.Object : BasicMatFinder.Object);
}

void AGRTGizmoActor::ApplyGizmoColor(UMaterialInstanceDynamic* DMI, const FLinearColor& InColor)
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

void AGRTGizmoActor::EnsureDynamicMaterials()
{
	if (MatSplinePoint)
	{
		return;
	}

	if (!GizmoBaseMaterial)
	{
		UE_LOG(LogGRT, Warning,
			TEXT("Footprint handle actor has no base material — handles will render with the default material."));
		return;
	}

	MatSplinePoint = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatSplinePointHover = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);

	ApplyGizmoColor(MatSplinePoint, FLinearColor(0.1f, 0.95f, 0.95f, 1.0f));
	ApplyGizmoColor(MatSplinePointHover, FLinearColor(2.5f, 2.5f, 0.3f, 1.0f));
}

/**
 * MIDs are built per spawned actor, and never in the constructor.
 *
 * A constructor runs against the CDO, so a MID created there is owned by the CDO and every
 * spawned instance inherits the same pointer — highlighting one handle would recolour every
 * instance's handles. It only looked correct because there is rarely more than one of these alive
 * at a time.
 */
void AGRTGizmoActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureDynamicMaterials();
}

void AGRTGizmoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureDynamicMaterials();
}

void AGRTGizmoActor::UpdateSplinePointHandles(const TArray<FVector>& LocalSplinePoints, const FTransform& BuildingTransform)
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

void AGRTGizmoActor::SetHighlightedSplinePoint(int32 PointIndex)
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

int32 AGRTGizmoActor::GetSplinePointIndexFromComponent(const UPrimitiveComponent* Component) const
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

void AGRTGizmoActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Dynamically scale with camera distance for uniform on-screen appearance -- handle size
	// (UpdateSplinePointHandles) reads GetActorScale3D() to stay consistent with this.
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const float Dist = FVector::Dist(Cam->GetCameraLocation(), GetActorLocation());
		const float Scale = FMath::Clamp(Dist * 0.0006f, 0.3f, 4.0f);
		SetActorScale3D(FVector(Scale));
	}
}
