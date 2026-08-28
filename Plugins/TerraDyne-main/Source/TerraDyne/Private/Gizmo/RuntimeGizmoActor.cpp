// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Gizmo/RuntimeGizmoActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameters.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "TerraDyneModule.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float ShaftDistance = 65.0f;
	constexpr float TipDistance = 160.0f;
	constexpr float ScaleHandleDistance = 150.0f;
	constexpr float PlaneHandleOffset = 45.0f;
	constexpr float RotateRingScale = 1.5f;
	constexpr float ScreenRingScaleMultiplier = 1.3f;

	// Native ring radius, empirically measured from SM_RotationHandle's actual bounds
	constexpr float NativeRingRadius = 92.0f;

	// Plane handle half-size (Cube is 100 units, scaled by 0.22 -> 22 units wide, half-size is 11.0f).
	constexpr float PlaneHandleHalfSize = 11.0f;

	// Point-like handles (Center, ScaleUniform)
	constexpr float PointHandleReach = 30.0f;
}

ARuntimeGizmoActor::ARuntimeGizmoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RotationHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_RotationHandle.SM_RotationHandle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RotationHandleFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Meshes/SM_RotationHandle.SM_RotationHandle"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScaleHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_ScaleHandle.SM_ScaleHandle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScaleHandleFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Meshes/SM_ScaleHandle.SM_ScaleHandle"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> UniformScaleHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_UniformScaleHandle.SM_UniformScaleHandle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UniformScaleHandleFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Meshes/SM_UniformScaleHandle.SM_UniformScaleHandle"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenTranslationHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_ScreenTranslationHandle.SM_ScreenTranslationHandle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenTranslationHandleFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Meshes/SM_ScreenTranslationHandle.SM_ScreenTranslationHandle"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenRotationHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_ScreenRotationHandle.SM_ScreenRotationHandle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScreenRotationHandleFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Meshes/SM_ScreenRotationHandle.SM_ScreenRotationHandle"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GizmoMatFinder(TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForegroundMatFinder(TEXT("/Game/Gizmo/Materials/M_GizmoAxis.M_GizmoAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForegroundMatFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/M_GizmoAxis.M_GizmoAxis"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatXFinder(TEXT("/Game/Gizmo/Materials/MI_XAxis.MI_XAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatXFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_XAxis.MI_XAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatYFinder(TEXT("/Game/Gizmo/Materials/MI_YAxis.MI_YAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatYFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_YAxis.MI_YAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatZFinder(TEXT("/Game/Gizmo/Materials/MI_ZAxis.MI_ZAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatZFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_ZAxis.MI_ZAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatXYZFinder(TEXT("/Game/Gizmo/Materials/MI_XYZAxis.MI_XYZAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatXYZFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_XYZAxis.MI_XYZAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatHoverFinder(TEXT("/Game/Gizmo/Materials/MI_Hovered.MI_Hovered"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatHoverFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Hovered.MI_Hovered"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotXFinder(TEXT("/Game/Gizmo/Materials/MI_Rotation_XAxis.MI_Rotation_XAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotXFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_XAxis.MI_Rotation_XAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotYFinder(TEXT("/Game/Gizmo/Materials/MI_Rotation_YAxis.MI_Rotation_YAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotYFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_YAxis.MI_Rotation_YAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotZFinder(TEXT("/Game/Gizmo/Materials/MI_Rotation_ZAxis.MI_Rotation_ZAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotZFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_ZAxis.MI_Rotation_ZAxis"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotHoverFinder(TEXT("/Game/Gizmo/Materials/MI_Rotation_Hovered.MI_Rotation_Hovered"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatRotHoverFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_Hovered.MI_Rotation_Hovered"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatScreenRotFinder(TEXT("/Game/Gizmo/Materials/M_ScreenRotation.M_ScreenRotation"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatScreenRotFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/M_ScreenRotation.M_ScreenRotation"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatScreenRotHoverFinder(TEXT("/Game/Gizmo/Materials/MI_ScreenRotation_Hovered.MI_ScreenRotation_Hovered"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatScreenRotHoverFinder2(TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_ScreenRotation_Hovered.MI_ScreenRotation_Hovered"));

	MatInstX = MatXFinder.Object ? MatXFinder.Object.Get() : MatXFinder2.Object.Get();
	MatInstY = MatYFinder.Object ? MatYFinder.Object.Get() : MatYFinder2.Object.Get();
	MatInstZ = MatZFinder.Object ? MatZFinder.Object.Get() : MatZFinder2.Object.Get();
	MatInstNeutral = MatXYZFinder.Object ? MatXYZFinder.Object.Get() : MatXYZFinder2.Object.Get();
	MatInstHovered = MatHoverFinder.Object ? MatHoverFinder.Object.Get() : MatHoverFinder2.Object.Get();
	MatInstRotX = MatRotXFinder.Object ? MatRotXFinder.Object.Get() : MatRotXFinder2.Object.Get();
	MatInstRotY = MatRotYFinder.Object ? MatRotYFinder.Object.Get() : MatRotYFinder2.Object.Get();
	MatInstRotZ = MatRotZFinder.Object ? MatRotZFinder.Object.Get() : MatRotZFinder2.Object.Get();
	MatInstRotHovered = MatRotHoverFinder.Object ? MatRotHoverFinder.Object.Get() : MatRotHoverFinder2.Object.Get();
	MatInstScreenRot = MatScreenRotFinder.Object ? MatScreenRotFinder.Object.Get() : MatScreenRotFinder2.Object.Get();
	MatInstScreenRotHovered = MatScreenRotHoverFinder.Object ? MatScreenRotHoverFinder.Object.Get() : MatScreenRotHoverFinder2.Object.Get();

	UStaticMesh* CylinderMesh = CylinderFinder.Object.Get();
	UStaticMesh* ConeMesh = ConeFinder.Object.Get();
	UStaticMesh* SphereMesh = SphereFinder.Object.Get();
	UStaticMesh* CubeMesh = CubeFinder.Object.Get();
	UStaticMesh* RotationHandleMesh = RotationHandleFinder.Object ? RotationHandleFinder.Object.Get() : RotationHandleFinder2.Object.Get();
	UStaticMesh* ScreenRotationHandleMesh = ScreenRotationHandleFinder.Object ? ScreenRotationHandleFinder.Object.Get() : (ScreenRotationHandleFinder2.Object ? ScreenRotationHandleFinder2.Object.Get() : RotationHandleMesh);
	UStaticMesh* ScaleHandleMesh = ScaleHandleFinder.Object ? ScaleHandleFinder.Object.Get() : ScaleHandleFinder2.Object.Get();
	UStaticMesh* UniformScaleHandleMesh = UniformScaleHandleFinder.Object ? UniformScaleHandleFinder.Object.Get() : UniformScaleHandleFinder2.Object.Get();
	UStaticMesh* ScreenTranslationHandleMesh = ScreenTranslationHandleFinder.Object ? ScreenTranslationHandleFinder.Object.Get() : (ScreenTranslationHandleFinder2.Object ? ScreenTranslationHandleFinder2.Object.Get() : CylinderMesh);

	UMaterialInterface* ForegroundMat = ForegroundMatFinder.Object ? ForegroundMatFinder.Object.Get() : ForegroundMatFinder2.Object.Get();
	UMaterialInterface* GizmoMat = GizmoMatFinder.Object ? GizmoMatFinder.Object.Get() : BasicMatFinder.Object.Get();
	if (MatInstX)
	{
		GizmoBaseMaterial = MatInstX;
	}
	else if (ForegroundMat)
	{
		GizmoBaseMaterial = ForegroundMat;
	}
	else
	{
		GizmoBaseMaterial = GizmoMat;
	}

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

	const FRotator XPrimaryToX = FRotator::ZeroRotator;
	const FRotator XPrimaryToY = FRotationMatrix::MakeFromX(FVector(0.0f, 1.0f, 0.0f)).Rotator();
	const FRotator XPrimaryToZ = FRotationMatrix::MakeFromX(FVector(0.0f, 0.0f, 1.0f)).Rotator();

	const FVector ShaftScale(0.06f, 0.06f, 1.1f);
	const FVector TipScale(0.22f, 0.22f, 0.4f);

	SetupMesh(MeshShaftX, TEXT("MeshShaftX"), CylinderMesh, FVector(ShaftDistance, 0.0f, 0.0f), RotX, ShaftScale);
	SetupMesh(MeshTipX, TEXT("MeshTipX"), ConeMesh, FVector(140.0f, 0.0f, 0.0f), RotX, TipScale);
	SetupMesh(MeshShaftY, TEXT("MeshShaftY"), CylinderMesh, FVector(0.0f, ShaftDistance, 0.0f), RotY, ShaftScale);
	SetupMesh(MeshTipY, TEXT("MeshTipY"), ConeMesh, FVector(0.0f, 140.0f, 0.0f), RotY, TipScale);
	SetupMesh(MeshShaftZ, TEXT("MeshShaftZ"), CylinderMesh, FVector(0.0f, 0.0f, ShaftDistance), RotZ, ShaftScale);
	SetupMesh(MeshTipZ, TEXT("MeshTipZ"), ConeMesh, FVector(0.0f, 0.0f, 140.0f), RotZ, TipScale);

	// Planar translation quads (flat along their normal axis, placed halfway along adjacent axes)
	const FVector PlaneScaleYZ(0.01f, 0.22f, 0.22f); // flat in X
	const FVector PlaneScaleXZ(0.22f, 0.01f, 0.22f); // flat in Y
	const FVector PlaneScaleXY(0.22f, 0.22f, 0.01f); // flat in Z

	SetupMesh(MeshPlaneYZ, TEXT("MeshPlaneYZ"), CubeMesh, FVector(0.0f, PlaneHandleOffset, PlaneHandleOffset), FRotator::ZeroRotator, PlaneScaleYZ);
	SetupMesh(MeshPlaneXZ, TEXT("MeshPlaneXZ"), CubeMesh, FVector(PlaneHandleOffset, 0.0f, PlaneHandleOffset), FRotator::ZeroRotator, PlaneScaleXZ);
	SetupMesh(MeshPlaneXY, TEXT("MeshPlaneXY"), CubeMesh, FVector(PlaneHandleOffset, PlaneHandleOffset, 0.0f), FRotator::ZeroRotator, PlaneScaleXY);

	const FVector CenterScale = (ScreenTranslationHandleMesh == CylinderMesh) ? FVector(0.25f, 0.25f, 0.01f) : FVector(1.0f);
	SetupMesh(MeshCenter, TEXT("MeshCenter"), ScreenTranslationHandleMesh, FVector::ZeroVector, FRotator::ZeroRotator, CenterScale);

	SetupMesh(MeshRotateX, TEXT("MeshRotateX"), RotationHandleMesh, FVector::ZeroVector, XPrimaryToX, FVector(RotateRingScale));
	SetupMesh(MeshRotateY, TEXT("MeshRotateY"), RotationHandleMesh, FVector::ZeroVector, XPrimaryToY, FVector(RotateRingScale));
	SetupMesh(MeshRotateZ, TEXT("MeshRotateZ"), RotationHandleMesh, FVector::ZeroVector, XPrimaryToZ, FVector(RotateRingScale));
	const float ScreenRotScale = (ScreenRotationHandleMesh == RotationHandleMesh) ? (RotateRingScale * ScreenRingScaleMultiplier) : RotateRingScale;
	SetupMesh(MeshRotateScreen, TEXT("MeshRotateScreen"), ScreenRotationHandleMesh, FVector::ZeroVector, FRotator::ZeroRotator, FVector(ScreenRotScale));

	SetupMesh(MeshScaleX, TEXT("MeshScaleX"), ScaleHandleMesh, FVector::ZeroVector, XPrimaryToX, FVector(1.2f));
	SetupMesh(MeshScaleY, TEXT("MeshScaleY"), ScaleHandleMesh, FVector::ZeroVector, XPrimaryToY, FVector(1.2f));
	SetupMesh(MeshScaleZ, TEXT("MeshScaleZ"), ScaleHandleMesh, FVector::ZeroVector, XPrimaryToZ, FVector(1.2f));
	SetupMesh(MeshScaleUniform, TEXT("MeshScaleUniform"), UniformScaleHandleMesh, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.8f));

	BuildHandleTypeMap();
}

void ARuntimeGizmoActor::BuildHandleTypeMap()
{
	HandleTypeByComponent.Reset();
	ComponentByHandleType.Reset();

	auto Tag = [this](UPrimitiveComponent* Comp, EGizmoHandleType Handle, bool bIsRepresentative = true)
	{
		if (!Comp) return;
		HandleTypeByComponent.Add(Comp, Handle);
		if (bIsRepresentative)
		{
			ComponentByHandleType.Add(Handle, Comp);
		}
	};

	// Both pieces of an arrow share one handle identity; the tip is the representative component
	// for the reverse lookup (more visually distinctive than the shaft for, e.g., a debug overlay).
	Tag(MeshShaftX, EGizmoHandleType::TranslateX, false);
	Tag(MeshTipX, EGizmoHandleType::TranslateX);
	Tag(MeshShaftY, EGizmoHandleType::TranslateY, false);
	Tag(MeshTipY, EGizmoHandleType::TranslateY);
	Tag(MeshShaftZ, EGizmoHandleType::TranslateZ, false);
	Tag(MeshTipZ, EGizmoHandleType::TranslateZ);
	Tag(MeshPlaneXY, EGizmoHandleType::PlaneXY);
	Tag(MeshPlaneXZ, EGizmoHandleType::PlaneXZ);
	Tag(MeshPlaneYZ, EGizmoHandleType::PlaneYZ);
	Tag(MeshCenter, EGizmoHandleType::Center);
	Tag(MeshRotateX, EGizmoHandleType::RotateX);
	Tag(MeshRotateY, EGizmoHandleType::RotateY);
	Tag(MeshRotateZ, EGizmoHandleType::RotateZ);
	Tag(MeshRotateScreen, EGizmoHandleType::RotateScreen);
	Tag(MeshScaleX, EGizmoHandleType::ScaleX);
	Tag(MeshScaleY, EGizmoHandleType::ScaleY);
	Tag(MeshScaleZ, EGizmoHandleType::ScaleZ);
	Tag(MeshScaleUniform, EGizmoHandleType::ScaleUniform);
}

EGizmoHandleType ARuntimeGizmoActor::GetHandleType(const UPrimitiveComponent* Component) const
{
	if (!Component) return EGizmoHandleType::None;
	if (const EGizmoHandleType* Found = HandleTypeByComponent.Find(Component))
	{
		return *Found;
	}
	return EGizmoHandleType::None;
}

UPrimitiveComponent* ARuntimeGizmoActor::GetComponentForHandle(EGizmoHandleType Handle) const
{
	if (const TWeakObjectPtr<UPrimitiveComponent>* Found = ComponentByHandleType.Find(Handle))
	{
		return Found->Get();
	}
	return nullptr;
}

double ARuntimeGizmoActor::GetHandleReach(EGizmoHandleType Handle) const
{
	const double ActorScale = GetActorScale3D().X;
	switch (Handle)
	{
	case EGizmoHandleType::TranslateX:
	case EGizmoHandleType::TranslateY:
	case EGizmoHandleType::TranslateZ:
		return TipDistance * ActorScale;
	case EGizmoHandleType::ScaleX:
	case EGizmoHandleType::ScaleY:
	case EGizmoHandleType::ScaleZ:
		return ScaleHandleDistance * ActorScale;
	case EGizmoHandleType::RotateX:
	case EGizmoHandleType::RotateY:
	case EGizmoHandleType::RotateZ:
		return NativeRingRadius * RotateRingScale * ActorScale;
	case EGizmoHandleType::RotateScreen:
		return NativeRingRadius * RotateRingScale * ScreenRingScaleMultiplier * ActorScale;
	case EGizmoHandleType::PlaneXY:
	case EGizmoHandleType::PlaneXZ:
	case EGizmoHandleType::PlaneYZ:
		return PlaneHandleHalfSize * ActorScale;
	case EGizmoHandleType::Center:
	case EGizmoHandleType::ScaleUniform:
		return PointHandleReach * ActorScale;
	default:
		return 0.0;
	}
}

void ARuntimeGizmoActor::ApplyGizmoColor(UMaterialInstanceDynamic* DMI, const FLinearColor& InColor)
{
	if (!DMI) return;
	DMI->SetVectorParameterValue(TEXT("GizmoColor"), InColor);
	DMI->SetVectorParameterValue(TEXT("Color"), InColor);
	DMI->SetVectorParameterValue(TEXT("BaseColor"), InColor);
	DMI->SetVectorParameterValue(TEXT("TintColor"), InColor);
	DMI->SetVectorParameterValue(TEXT("Tint"), InColor);
	DMI->SetVectorParameterValue(TEXT("EmissiveColor"), InColor * 2.5f);
}

void ARuntimeGizmoActor::EnsureDynamicMaterials()
{
	if (!MatInstX)
	{
		MatInstX = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_XAxis.MI_XAxis"));
		if (!MatInstX) MatInstX = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_XAxis.MI_XAxis"));

		MatInstY = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_YAxis.MI_YAxis"));
		if (!MatInstY) MatInstY = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_YAxis.MI_YAxis"));

		MatInstZ = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_ZAxis.MI_ZAxis"));
		if (!MatInstZ) MatInstZ = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_ZAxis.MI_ZAxis"));

		MatInstNeutral = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_XYZAxis.MI_XYZAxis"));
		if (!MatInstNeutral) MatInstNeutral = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_XYZAxis.MI_XYZAxis"));

		MatInstHovered = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_Hovered.MI_Hovered"));
		if (!MatInstHovered) MatInstHovered = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Hovered.MI_Hovered"));

		MatInstRotX = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_Rotation_XAxis.MI_Rotation_XAxis"));
		if (!MatInstRotX) MatInstRotX = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_XAxis.MI_Rotation_XAxis"));

		MatInstRotY = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_Rotation_YAxis.MI_Rotation_YAxis"));
		if (!MatInstRotY) MatInstRotY = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_YAxis.MI_Rotation_YAxis"));

		MatInstRotZ = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_Rotation_ZAxis.MI_Rotation_ZAxis"));
		if (!MatInstRotZ) MatInstRotZ = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_ZAxis.MI_Rotation_ZAxis"));

		MatInstRotHovered = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_Rotation_Hovered.MI_Rotation_Hovered"));
		if (!MatInstRotHovered) MatInstRotHovered = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_Rotation_Hovered.MI_Rotation_Hovered"));

		MatInstScreenRot = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/M_ScreenRotation.M_ScreenRotation"));
		if (!MatInstScreenRot) MatInstScreenRot = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/M_ScreenRotation.M_ScreenRotation"));

		MatInstScreenRotHovered = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/MI_ScreenRotation_Hovered.MI_ScreenRotation_Hovered"));
		if (!MatInstScreenRotHovered) MatInstScreenRotHovered = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/MI_ScreenRotation_Hovered.MI_ScreenRotation_Hovered"));
	}

	if (!MatX)
	{
		UMaterialInterface* BaseMat = MatInstX ? MatInstX.Get() : GizmoBaseMaterial.Get();
		if (!BaseMat)
		{
			BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Gizmo/Materials/M_GizmoAxis.M_GizmoAxis"));
			if (!BaseMat)
			{
				BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mytaverse/TransformGizmo/Materials/M_GizmoAxis.M_GizmoAxis"));
			}
			if (!BaseMat)
			{
				BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
			}
			if (!BaseMat)
			{
				BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			}
		}

		if (BaseMat)
		{
			MatX = UMaterialInstanceDynamic::Create(MatInstX ? MatInstX.Get() : BaseMat, this);
			MatY = UMaterialInstanceDynamic::Create(MatInstY ? MatInstY.Get() : BaseMat, this);
			MatZ = UMaterialInstanceDynamic::Create(MatInstZ ? MatInstZ.Get() : BaseMat, this);
			MatPlaneXY = UMaterialInstanceDynamic::Create(MatInstZ ? MatInstZ.Get() : BaseMat, this);
			MatPlaneXZ = UMaterialInstanceDynamic::Create(MatInstY ? MatInstY.Get() : BaseMat, this);
			MatPlaneYZ = UMaterialInstanceDynamic::Create(MatInstX ? MatInstX.Get() : BaseMat, this);
			MatNeutral = UMaterialInstanceDynamic::Create(MatInstNeutral ? MatInstNeutral.Get() : BaseMat, this);

			ApplyGizmoColor(MatX, FLinearColor(1.0f, 0.08f, 0.08f, 1.0f));
			ApplyGizmoColor(MatY, FLinearColor(0.08f, 0.95f, 0.08f, 1.0f));
			ApplyGizmoColor(MatZ, FLinearColor(0.12f, 0.45f, 1.0f, 1.0f));
			ApplyGizmoColor(MatPlaneYZ, FLinearColor(1.0f, 0.08f, 0.08f, 1.0f)); // Red for YZ plane
			ApplyGizmoColor(MatPlaneXZ, FLinearColor(0.08f, 0.95f, 0.08f, 1.0f)); // Green for XZ plane
			ApplyGizmoColor(MatPlaneXY, FLinearColor(0.12f, 0.45f, 1.0f, 1.0f)); // Blue for XY plane
			ApplyGizmoColor(MatNeutral, FLinearColor(2.5f, 2.5f, 2.5f, 1.0f));
			MatNeutral->SetScalarParameterValue(TEXT("Alpha"), 1.0f);
		}
	}

	ApplyMaterialsToComponents();
}

void ARuntimeGizmoActor::ApplyMaterialsToComponents()
{
	auto SetMat = [](UStaticMeshComponent* Comp, UMaterialInterface* Mat)
	{
		if (!Comp || !Mat) return;
		const int32 NumMaterials = FMath::Max(1, Comp->GetNumMaterials());
		for (int32 i = 0; i < NumMaterials; ++i)
		{
			Comp->SetMaterial(i, Mat);
		}
	};

	const bool bHoverX = (CurrentHighlight == EGizmoHandleType::TranslateX || CurrentHighlight == EGizmoHandleType::ScaleX);
	const bool bHoverY = (CurrentHighlight == EGizmoHandleType::TranslateY || CurrentHighlight == EGizmoHandleType::ScaleY);
	const bool bHoverZ = (CurrentHighlight == EGizmoHandleType::TranslateZ || CurrentHighlight == EGizmoHandleType::ScaleZ);
	const bool bHoverPlaneXY = (CurrentHighlight == EGizmoHandleType::PlaneXY);
	const bool bHoverPlaneXZ = (CurrentHighlight == EGizmoHandleType::PlaneXZ);
	const bool bHoverPlaneYZ = (CurrentHighlight == EGizmoHandleType::PlaneYZ);
	const bool bHoverCenter = (CurrentHighlight == EGizmoHandleType::Center || CurrentHighlight == EGizmoHandleType::ScaleUniform);
	const bool bHoverRotX = (CurrentHighlight == EGizmoHandleType::RotateX);
	const bool bHoverRotY = (CurrentHighlight == EGizmoHandleType::RotateY);
	const bool bHoverRotZ = (CurrentHighlight == EGizmoHandleType::RotateZ);
	const bool bHoverScreenRot = (CurrentHighlight == EGizmoHandleType::RotateScreen);

	UMaterialInterface* BaseMat = MatInstX ? MatInstX.Get() : GizmoBaseMaterial.Get();
	UMaterialInterface* HoverMat = MatInstHovered ? MatInstHovered.Get() : (MatNeutral ? MatNeutral.Get() : BaseMat);
	UMaterialInterface* RotHoverMat = MatInstRotHovered ? MatInstRotHovered.Get() : HoverMat;
	UMaterialInterface* ScreenRotHoverMat = MatInstScreenRotHovered ? MatInstScreenRotHovered.Get() : HoverMat;
	UMaterialInterface* CenterMat = MatNeutral ? MatNeutral.Get() : (MatInstNeutral ? MatInstNeutral.Get() : BaseMat);

	UMaterialInterface* MatOrInstX = MatX ? MatX.Get() : (MatInstX ? MatInstX.Get() : BaseMat);
	UMaterialInterface* MatOrInstY = MatY ? MatY.Get() : (MatInstY ? MatInstY.Get() : BaseMat);
	UMaterialInterface* MatOrInstZ = MatZ ? MatZ.Get() : (MatInstZ ? MatInstZ.Get() : BaseMat);
	UMaterialInterface* MatOrInstPlaneYZ = MatPlaneYZ ? MatPlaneYZ.Get() : (MatInstX ? MatInstX.Get() : BaseMat);
	UMaterialInterface* MatOrInstPlaneXZ = MatPlaneXZ ? MatPlaneXZ.Get() : (MatInstY ? MatInstY.Get() : BaseMat);
	UMaterialInterface* MatOrInstPlaneXY = MatPlaneXY ? MatPlaneXY.Get() : (MatInstZ ? MatInstZ.Get() : BaseMat);
	UMaterialInterface* MatOrInstRotX = MatInstRotX ? MatInstRotX.Get() : (MatX ? MatX.Get() : BaseMat);
	UMaterialInterface* MatOrInstRotY = MatInstRotY ? MatInstRotY.Get() : (MatY ? MatY.Get() : BaseMat);
	UMaterialInterface* MatOrInstRotZ = MatInstRotZ ? MatInstRotZ.Get() : (MatZ ? MatZ.Get() : BaseMat);
	UMaterialInterface* MatOrInstScreenRot = MatInstScreenRot ? MatInstScreenRot.Get() : (MatNeutral ? MatNeutral.Get() : BaseMat);

	SetMat(MeshShaftX, bHoverX ? HoverMat : MatOrInstX);
	SetMat(MeshTipX, bHoverX ? HoverMat : MatOrInstX);
	SetMat(MeshScaleX, bHoverX ? HoverMat : MatOrInstX);

	SetMat(MeshShaftY, bHoverY ? HoverMat : MatOrInstY);
	SetMat(MeshTipY, bHoverY ? HoverMat : MatOrInstY);
	SetMat(MeshScaleY, bHoverY ? HoverMat : MatOrInstY);

	SetMat(MeshShaftZ, bHoverZ ? HoverMat : MatOrInstZ);
	SetMat(MeshTipZ, bHoverZ ? HoverMat : MatOrInstZ);
	SetMat(MeshScaleZ, bHoverZ ? HoverMat : MatOrInstZ);

	SetMat(MeshPlaneYZ, bHoverPlaneYZ ? HoverMat : MatOrInstPlaneYZ);
	SetMat(MeshPlaneXZ, bHoverPlaneXZ ? HoverMat : MatOrInstPlaneXZ);
	SetMat(MeshPlaneXY, bHoverPlaneXY ? HoverMat : MatOrInstPlaneXY);

	SetMat(MeshCenter, bHoverCenter ? HoverMat : CenterMat);
	SetMat(MeshScaleUniform, bHoverCenter ? HoverMat : CenterMat);

	SetMat(MeshRotateX, bHoverRotX ? RotHoverMat : MatOrInstRotX);
	SetMat(MeshRotateY, bHoverRotY ? RotHoverMat : MatOrInstRotY);
	SetMat(MeshRotateZ, bHoverRotZ ? RotHoverMat : MatOrInstRotZ);
	SetMat(MeshRotateScreen, bHoverScreenRot ? ScreenRotHoverMat : MatOrInstScreenRot);
}

void ARuntimeGizmoActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureDynamicMaterials();
	SetMode(CurrentMode);
}

void ARuntimeGizmoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureDynamicMaterials();
}

void ARuntimeGizmoActor::SetMode(EGizmoMode NewMode)
{
	CurrentMode = NewMode;

	auto SetActive = [](UPrimitiveComponent* Comp, bool bActive)
	{
		if (!Comp) return;
		Comp->SetVisibility(bActive, true);
		Comp->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	};

	const bool bTranslate = (NewMode == EGizmoMode::Translate);
	const bool bRotate = (NewMode == EGizmoMode::Rotate);
	const bool bScale = (NewMode == EGizmoMode::Scale);

	SetActive(MeshShaftX, bTranslate); SetActive(MeshTipX, bTranslate);
	SetActive(MeshShaftY, bTranslate); SetActive(MeshTipY, bTranslate);
	SetActive(MeshShaftZ, bTranslate); SetActive(MeshTipZ, bTranslate);
	SetActive(MeshPlaneXY, bTranslate);
	SetActive(MeshPlaneXZ, bTranslate);
	SetActive(MeshPlaneYZ, bTranslate);
	SetActive(MeshCenter, bTranslate);

	SetActive(MeshRotateX, bRotate);
	SetActive(MeshRotateY, bRotate);
	SetActive(MeshRotateZ, bRotate);
	SetActive(MeshRotateScreen, bRotate);

	SetActive(MeshScaleX, bScale);
	SetActive(MeshScaleY, bScale);
	SetActive(MeshScaleZ, bScale);
	SetActive(MeshScaleUniform, bScale);

	ApplyMaterialsToComponents();
}

void ARuntimeGizmoActor::SetHighlightedHandle(EGizmoHandleType Handle)
{
	EnsureDynamicMaterials();

	if (CurrentHighlight == Handle) return;
	CurrentHighlight = Handle;

	ApplyMaterialsToComponents();
}

void ARuntimeGizmoActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const FVector CamLoc = Cam->GetCameraLocation();
		const float Dist = FVector::Dist(CamLoc, GetActorLocation());
		const float Scale = FMath::Clamp(Dist * 0.0006f, 0.3f, 4.0f);
		SetActorScale3D(FVector(Scale));

		const FVector ToCamera = (CamLoc - GetActorLocation()).GetSafeNormal();
		if (!ToCamera.IsNearlyZero())
		{
			// The screen-space rotation ring always faces the camera
			if (MeshRotateScreen && MeshRotateScreen->IsVisible())
			{
				MeshRotateScreen->SetWorldRotation(FRotationMatrix::MakeFromX(ToCamera).Rotator());
			}

			// The screen-space center handle disk always faces the camera
			if (MeshCenter && MeshCenter->IsVisible())
			{
				const UStaticMesh* CenterMesh = MeshCenter->GetStaticMesh();
				if (CenterMesh && CenterMesh->GetName().Contains(TEXT("Cylinder")))
				{
					MeshCenter->SetWorldRotation(FRotationMatrix::MakeFromZ(ToCamera).Rotator());
				}
				else
				{
					MeshCenter->SetWorldRotation(FRotationMatrix::MakeFromX(ToCamera).Rotator());
				}
			}
		}
	}
}
