// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Gizmo/RuntimeGizmoActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "TerraDyneModule.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Placeholder placement constants -- the migrated meshes' native size/pivot hasn't been seen in
	// a rendered viewport from this environment, so these are a reasonable first pass, not a
	// visually-tuned final. GizmoActorTests.cpp logs each migrated mesh's real bounds so a follow-up
	// pass can correct these against ground truth instead of guessing twice.
	constexpr float ShaftDistance = 100.0f;
	constexpr float TipDistance = 200.0f;
	constexpr float ScaleHandleDistance = 150.0f;
	constexpr float PlaneHandleOffset = 50.0f;
	constexpr float RotateRingScale = 1.5f;
	constexpr float ScreenRingScaleMultiplier = 1.3f;

	// Native ring radius, empirically measured (not guessed) from SM_RotationHandle's actual bounds
	// extent logged by TerraDyne.Gizmo.ActorRuntimeAssets (Y=Z=92 -- see that test's AddInfo output).
	constexpr float NativeRingRadius = 92.0f;

	// Cube's native half-size (/Engine/BasicShapes/Cube.Cube is 100 units per side); PlaneScale's
	// 0.4 XY component (see the constructor) scales that down to this actual plane-handle half-size.
	constexpr float PlaneHandleHalfSize = 50.0f * 0.4f;

	// Point-like handles (Center, ScaleUniform) have no directional mesh dimension to derive a size
	// from the way a segment/ring/quad does -- a fixed, generous click radius stands in instead.
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ScaleHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_ScaleHandle.SM_ScaleHandle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UniformScaleHandleFinder(TEXT("/Game/Gizmo/Meshes/SM_UniformScaleHandle.SM_UniformScaleHandle"));

	// Same fallback chain as ATerraDyneGizmoActor -- M_OverlayForeground (translucent, disable-
	// depth-test, so the gizmo renders through terrain/walls) first, then engine gizmo/basic-shape
	// materials as runtime-safe fallbacks if the project material is ever missing.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GizmoMatFinder(TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForegroundMatFinder(TEXT("/Game/Materials/M_OverlayForeground.M_OverlayForeground"));

	UStaticMesh* CylinderMesh = CylinderFinder.Object;
	UStaticMesh* ConeMesh = ConeFinder.Object;
	UStaticMesh* SphereMesh = SphereFinder.Object;
	UStaticMesh* CubeMesh = CubeFinder.Object;
	UStaticMesh* RotationHandleMesh = RotationHandleFinder.Object;
	UStaticMesh* ScaleHandleMesh = ScaleHandleFinder.Object;
	UStaticMesh* UniformScaleHandleMesh = UniformScaleHandleFinder.Object;

	GizmoBaseMaterial = ForegroundMatFinder.Object
		? ForegroundMatFinder.Object
		: (GizmoMatFinder.Object ? GizmoMatFinder.Object : BasicMatFinder.Object);

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
		Comp->DepthPriorityGroup = SDPG_Foreground;
	};

	// A mesh whose "length" axis is local Z by default (the engine's own Cylinder/Cone) needs this
	// rotation to point along each world axis.
	const FRotator RotX = FRotationMatrix::MakeFromZ(FVector(1.0f, 0.0f, 0.0f)).Rotator();
	const FRotator RotY = FRotationMatrix::MakeFromZ(FVector(0.0f, 1.0f, 0.0f)).Rotator();
	const FRotator RotZ = FRotator::ZeroRotator;

	// The migrated SM_RotationHandle/SM_ScaleHandle meshes are authored with their own primary axis
	// (a ring's normal; a scale handle's long arm) along local X, not Z -- confirmed by
	// GizmoActorTests.cpp's logged bounds extent (SM_RotationHandle: X=2, Y=92, Z=92; SM_ScaleHandle:
	// X=58.5, Y=7, Z=7 -- both thin/short in X only when X is what actually needs to be zero-rotation-
	// aligned, i.e. their long/normal axis IS X). Reusing RotX/RotY/RotZ above for these meshes -- as
	// the code originally did, before that bounds data existed -- pointed every ring/handle at the
	// wrong world axis.
	const FRotator XPrimaryToX = FRotator::ZeroRotator;
	const FRotator XPrimaryToY = FRotationMatrix::MakeFromX(FVector(0.0f, 1.0f, 0.0f)).Rotator();
	const FRotator XPrimaryToZ = FRotationMatrix::MakeFromX(FVector(0.0f, 0.0f, 1.0f)).Rotator();

	const FVector ShaftScale(0.06f, 0.06f, 2.0f);
	const FVector TipScale(0.22f, 0.22f, 0.5f);

	SetupMesh(MeshShaftX, TEXT("MeshShaftX"), CylinderMesh, FVector(ShaftDistance, 0.0f, 0.0f), RotX, ShaftScale);
	SetupMesh(MeshTipX, TEXT("MeshTipX"), ConeMesh, FVector(TipDistance, 0.0f, 0.0f), RotX, TipScale);
	SetupMesh(MeshShaftY, TEXT("MeshShaftY"), CylinderMesh, FVector(0.0f, ShaftDistance, 0.0f), RotY, ShaftScale);
	SetupMesh(MeshTipY, TEXT("MeshTipY"), ConeMesh, FVector(0.0f, TipDistance, 0.0f), RotY, TipScale);
	SetupMesh(MeshShaftZ, TEXT("MeshShaftZ"), CylinderMesh, FVector(0.0f, 0.0f, ShaftDistance), RotZ, ShaftScale);
	SetupMesh(MeshTipZ, TEXT("MeshTipZ"), ConeMesh, FVector(0.0f, 0.0f, TipDistance), RotZ, TipScale);

	const FVector PlaneScale(0.4f, 0.4f, 0.02f);
	SetupMesh(MeshPlaneXY, TEXT("MeshPlaneXY"), CubeMesh, FVector(PlaneHandleOffset, PlaneHandleOffset, 0.0f), FRotator::ZeroRotator, PlaneScale);
	SetupMesh(MeshPlaneXZ, TEXT("MeshPlaneXZ"), CubeMesh, FVector(PlaneHandleOffset, 0.0f, PlaneHandleOffset), FRotationMatrix::MakeFromZ(FVector(0.0f, 1.0f, 0.0f)).Rotator(), PlaneScale);
	SetupMesh(MeshPlaneYZ, TEXT("MeshPlaneYZ"), CubeMesh, FVector(0.0f, PlaneHandleOffset, PlaneHandleOffset), FRotationMatrix::MakeFromZ(FVector(1.0f, 0.0f, 0.0f)).Rotator(), PlaneScale);

	SetupMesh(MeshCenter, TEXT("MeshCenter"), SphereMesh, FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.3f));

	SetupMesh(MeshRotateX, TEXT("MeshRotateX"), RotationHandleMesh, FVector::ZeroVector, XPrimaryToX, FVector(RotateRingScale));
	SetupMesh(MeshRotateY, TEXT("MeshRotateY"), RotationHandleMesh, FVector::ZeroVector, XPrimaryToY, FVector(RotateRingScale));
	SetupMesh(MeshRotateZ, TEXT("MeshRotateZ"), RotationHandleMesh, FVector::ZeroVector, XPrimaryToZ, FVector(RotateRingScale));
	SetupMesh(MeshRotateScreen, TEXT("MeshRotateScreen"), RotationHandleMesh, FVector::ZeroVector, FRotator::ZeroRotator, FVector(RotateRingScale * ScreenRingScaleMultiplier));

	SetupMesh(MeshScaleX, TEXT("MeshScaleX"), ScaleHandleMesh, FVector(ScaleHandleDistance, 0.0f, 0.0f), XPrimaryToX, FVector(1.0f));
	SetupMesh(MeshScaleY, TEXT("MeshScaleY"), ScaleHandleMesh, FVector(0.0f, ScaleHandleDistance, 0.0f), XPrimaryToY, FVector(1.0f));
	SetupMesh(MeshScaleZ, TEXT("MeshScaleZ"), ScaleHandleMesh, FVector(0.0f, 0.0f, ScaleHandleDistance), XPrimaryToZ, FVector(1.0f));
	SetupMesh(MeshScaleUniform, TEXT("MeshScaleUniform"), UniformScaleHandleMesh, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.0f));

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
	DMI->SetVectorParameterValue(TEXT("EmissiveColor"), InColor * 2.5f);
}

void ARuntimeGizmoActor::EnsureDynamicMaterials()
{
	if (MatX) return;

	if (!GizmoBaseMaterial)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Gizmo has no base material -- handles will render with the default material."));
		return;
	}

	MatX = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatY = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatZ = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatPlaneXY = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatPlaneXZ = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatPlaneYZ = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);
	MatNeutral = UMaterialInstanceDynamic::Create(GizmoBaseMaterial, this);

	ApplyGizmoColor(MatX, FLinearColor(1.0f, 0.08f, 0.08f, 1.0f));
	ApplyGizmoColor(MatY, FLinearColor(0.08f, 0.95f, 0.08f, 1.0f));
	ApplyGizmoColor(MatZ, FLinearColor(0.12f, 0.45f, 1.0f, 1.0f));
	ApplyGizmoColor(MatPlaneXY, FLinearColor(0.9f, 0.85f, 0.1f, 1.0f));
	ApplyGizmoColor(MatPlaneXZ, FLinearColor(0.9f, 0.1f, 0.85f, 1.0f));
	ApplyGizmoColor(MatPlaneYZ, FLinearColor(0.1f, 0.85f, 0.9f, 1.0f));
	ApplyGizmoColor(MatNeutral, FLinearColor(0.9f, 0.9f, 0.9f, 1.0f));

	auto SetMat = [](UStaticMeshComponent* Comp, UMaterialInstanceDynamic* Mat) { if (Comp) Comp->SetMaterial(0, Mat); };
	SetMat(MeshShaftX, MatX); SetMat(MeshTipX, MatX); SetMat(MeshRotateX, MatX); SetMat(MeshScaleX, MatX);
	SetMat(MeshShaftY, MatY); SetMat(MeshTipY, MatY); SetMat(MeshRotateY, MatY); SetMat(MeshScaleY, MatY);
	SetMat(MeshShaftZ, MatZ); SetMat(MeshTipZ, MatZ); SetMat(MeshRotateZ, MatZ); SetMat(MeshScaleZ, MatZ);
	SetMat(MeshPlaneXY, MatPlaneXY);
	SetMat(MeshPlaneXZ, MatPlaneXZ);
	SetMat(MeshPlaneYZ, MatPlaneYZ);
	SetMat(MeshCenter, MatNeutral);
	SetMat(MeshRotateScreen, MatNeutral);
	SetMat(MeshScaleUniform, MatNeutral);
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
}

void ARuntimeGizmoActor::SetHighlightedHandle(EGizmoHandleType Handle)
{
	EnsureDynamicMaterials();

	if (CurrentHighlight == Handle) return;
	CurrentHighlight = Handle;

	const FLinearColor NormalX(1.0f, 0.08f, 0.08f, 1.0f);
	const FLinearColor NormalY(0.08f, 0.95f, 0.08f, 1.0f);
	const FLinearColor NormalZ(0.12f, 0.45f, 1.0f, 1.0f);
	const FLinearColor NormalPlaneXY(0.9f, 0.85f, 0.1f, 1.0f);
	const FLinearColor NormalPlaneXZ(0.9f, 0.1f, 0.85f, 1.0f);
	const FLinearColor NormalPlaneYZ(0.1f, 0.85f, 0.9f, 1.0f);
	const FLinearColor NormalNeutral(0.9f, 0.9f, 0.9f, 1.0f);
	const FLinearColor HighlightColor(2.5f, 2.5f, 0.4f, 1.0f);

	auto IsOneOf = [Handle](EGizmoHandleType A, EGizmoHandleType B, EGizmoHandleType C) { return Handle == A || Handle == B || Handle == C; };

	ApplyGizmoColor(MatX, IsOneOf(EGizmoHandleType::TranslateX, EGizmoHandleType::RotateX, EGizmoHandleType::ScaleX) ? HighlightColor : NormalX);
	ApplyGizmoColor(MatY, IsOneOf(EGizmoHandleType::TranslateY, EGizmoHandleType::RotateY, EGizmoHandleType::ScaleY) ? HighlightColor : NormalY);
	ApplyGizmoColor(MatZ, IsOneOf(EGizmoHandleType::TranslateZ, EGizmoHandleType::RotateZ, EGizmoHandleType::ScaleZ) ? HighlightColor : NormalZ);
	ApplyGizmoColor(MatPlaneXY, Handle == EGizmoHandleType::PlaneXY ? HighlightColor : NormalPlaneXY);
	ApplyGizmoColor(MatPlaneXZ, Handle == EGizmoHandleType::PlaneXZ ? HighlightColor : NormalPlaneXZ);
	ApplyGizmoColor(MatPlaneYZ, Handle == EGizmoHandleType::PlaneYZ ? HighlightColor : NormalPlaneYZ);
	const bool bNeutralActive = (Handle == EGizmoHandleType::Center || Handle == EGizmoHandleType::RotateScreen || Handle == EGizmoHandleType::ScaleUniform);
	ApplyGizmoColor(MatNeutral, bNeutralActive ? HighlightColor : NormalNeutral);
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

		// The screen-space rotation ring always faces the camera -- unlike every other handle, its
		// whole purpose is to rotate around whatever axis the camera happens to be looking down, so
		// its own orientation has to track the camera every frame rather than staying fixed.
		if (MeshRotateScreen && MeshRotateScreen->IsVisible())
		{
			const FVector ToCamera = (CamLoc - GetActorLocation()).GetSafeNormal();
			if (!ToCamera.IsNearlyZero())
			{
				// MakeFromX, not MakeFromZ: SM_RotationHandle's ring normal is local X (see the
				// constructor's comment on XPrimaryToX/Y/Z), so it's local X that has to point at
				// the camera for the ring's face to actually turn toward the viewer.
				MeshRotateScreen->SetWorldRotation(FRotationMatrix::MakeFromX(ToCamera).Rotator());
			}
		}
	}
}
