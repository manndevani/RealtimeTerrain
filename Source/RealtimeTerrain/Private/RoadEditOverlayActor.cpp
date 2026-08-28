// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadEditOverlayActor.h"

#include "RealtimeRoadSplineActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FLinearColor RouteColor(0.10f, 0.75f, 1.00f);
	const FLinearColor HandleColor(1.00f, 0.78f, 0.10f);
	const FLinearColor HandleHoverColor(1.00f, 1.00f, 1.00f);
	const FLinearColor HandleSelectedColor(1.00f, 0.30f, 0.05f);

	/** The engine's basic shapes are 100 units across, so scale is the desired size over 100. */
	constexpr double BasicShapeSize = 100.0;

	/** Writes a colour across the parameter names the two candidate base materials expose. */
	void ApplyOverlayColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color)
	{
		if (!Material) return;
		Material->SetVectorParameterValue(TEXT("GizmoColor"), Color);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
	}

	void ConfigureOverlayComponent(UPrimitiveComponent* Component, bool bHitTestable)
	{
		if (!Component) return;

		if (bHitTestable)
		{
			// Query only, and only against the channels a cursor trace uses: the overlay must be
			// grabbable without ever obstructing anything that actually drives on the road.
			Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Component->SetCollisionObjectType(ECC_WorldDynamic);
			Component->SetCollisionResponseToAllChannels(ECR_Ignore);
			Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		Component->SetCastShadow(false);
		Component->SetTranslucentSortPriority(10000);
		Component->SetGenerateOverlapEvents(false);
	}
}

ARoadEditOverlayActor::ARoadEditOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// GizmoMaterial lives in /Engine/EngineMaterials, which is cooked runtime content, and exposes
	// a GizmoColor parameter. The editor-only gizmo material trees would vanish in a package.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GizmoMatFinder(TEXT("/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	SphereMesh = SphereFinder.Object;
	OverlayBaseMaterial = GizmoMatFinder.Object ? GizmoMatFinder.Object : BasicMatFinder.Object;

	// Foreground material: Translucent + Unlit + Disable Depth Test, so selected handles and
	// the route line render through roads, terrain and walls. Falls back to the normal material
	// when the asset is missing so nothing breaks before it is created.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForegroundMatFinder(
		TEXT("/Game/Mytaverse/TransformGizmo/Materials/M_GizmoAxis.M_GizmoAxis"));
	OverlayForegroundMaterial = ForegroundMatFinder.Object
		? ForegroundMatFinder.Object
		: OverlayBaseMaterial;

	RouteSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RouteSegments"));
	RouteSegments->SetupAttachment(SceneRoot);
	RouteSegments->SetStaticMesh(CylinderFinder.Object);
	ConfigureOverlayComponent(RouteSegments, /*bHitTestable*/ false);
}

void ARoadEditOverlayActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureDynamicMaterials();
}

void ARoadEditOverlayActor::EnsureDynamicMaterials()
{
	if (MatRoute || !OverlayBaseMaterial)
	{
		return;
	}

	// The foreground material has Disable Depth Test, so anything using it renders through
	// roads, terrain and walls. Normal handles use the standard material and get occluded.
	UMaterialInterface* FgMat = OverlayForegroundMaterial ? OverlayForegroundMaterial : OverlayBaseMaterial;

	MatRoute         = UMaterialInstanceDynamic::Create(FgMat, this);
	MatHandle        = UMaterialInstanceDynamic::Create(OverlayBaseMaterial, this);
	MatHandleHover   = UMaterialInstanceDynamic::Create(FgMat, this);
	MatHandleSelected = UMaterialInstanceDynamic::Create(FgMat, this);

	ApplyOverlayColor(MatRoute, RouteColor);
	ApplyOverlayColor(MatHandle, HandleColor);
	ApplyOverlayColor(MatHandleHover, HandleHoverColor);
	ApplyOverlayColor(MatHandleSelected, HandleSelectedColor);

	if (RouteSegments && MatRoute)
	{
		RouteSegments->SetMaterial(0, MatRoute);
	}
}

void ARoadEditOverlayActor::ShowRoad(const ARealtimeRoadSplineActor* Road)
{
	EnsureDynamicMaterials();

	const USplineComponent* Spline = Road ? Road->SplineComponent : nullptr;
	const int32 NumPoints = Spline ? Spline->GetNumberOfSplinePoints() : 0;

	if (!Spline || NumPoints < 1)
	{
		if (RouteSegments)
		{
			RouteSegments->ClearInstances();
		}
		for (UStaticMeshComponent* Handle : PointHandles)
		{
			if (Handle)
			{
				Handle->DestroyComponent();
			}
		}
		PointHandles.Reset();
		return;
	}

	const FVector Lift(0.0, 0.0, VerticalOffset);

	//--- Route line ---

	RouteSegments->ClearInstances();

	if (NumPoints >= 2)
	{
		const double SplineLength = Spline->GetSplineLength();
		const double Spacing = FMath::Max(20.0f, RouteSampleSpacing);
		const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(SplineLength / Spacing));
		const double Step = SplineLength / static_cast<double>(NumSteps);
		const double RadialScale = FMath::Max(1.0f, RouteThickness) / BasicShapeSize;

		FVector Previous = Spline->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::World) + Lift;

		for (int32 i = 1; i <= NumSteps; ++i)
		{
			const double Distance = FMath::Min(i * Step, SplineLength);
			const FVector Current = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World) + Lift;

			const FVector Delta = Current - Previous;
			const double Length = Delta.Size();
			if (Length > UE_KINDA_SMALL_NUMBER)
			{
				// The cylinder runs along its local Z and is centred, so it spans the segment when
				// placed at the midpoint, aimed down the segment, and scaled by length over 100.
				const FTransform SegmentTransform(
					FRotationMatrix::MakeFromZ(Delta / Length).Rotator(),
					Previous + Delta * 0.5,
					FVector(RadialScale, RadialScale, Length / BasicShapeSize));

				RouteSegments->AddInstance(SegmentTransform, /*bWorldSpace*/ true);
			}

			Previous = Current;
		}
	}

	//--- Control point handles ---

	while (PointHandles.Num() < NumPoints)
	{
		const FName ComponentName = *FString::Printf(TEXT("RoadPointHandle_%d"), PointHandles.Num());
		UStaticMeshComponent* Handle = NewObject<UStaticMeshComponent>(this, ComponentName);
		if (!Handle)
		{
			break;
		}

		Handle->RegisterComponent();
		Handle->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
		Handle->SetStaticMesh(SphereMesh);
		ConfigureOverlayComponent(Handle, /*bHitTestable*/ true);
		PointHandles.Add(Handle);
	}

	while (PointHandles.Num() > NumPoints)
	{
		if (UStaticMeshComponent* Handle = PointHandles.Pop())
		{
			Handle->DestroyComponent();
		}
	}

	const double HandleScale = FMath::Max(1.0f, HandleDiameter) / BasicShapeSize;

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (UStaticMeshComponent* Handle = PointHandles[i])
		{
			Handle->SetWorldLocation(Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World) + Lift);
			Handle->SetWorldScale3D(FVector(HandleScale));
		}
	}

	RefreshHandleMaterials();
}

void ARoadEditOverlayActor::SetHighlightedPoint(int32 PointIndex)
{
	if (HighlightedPoint == PointIndex) return;
	HighlightedPoint = PointIndex;
	RefreshHandleMaterials();
}

void ARoadEditOverlayActor::SetSelectedPoint(int32 PointIndex)
{
	if (SelectedPoint == PointIndex) return;
	SelectedPoint = PointIndex;
	RefreshHandleMaterials();
}

void ARoadEditOverlayActor::RefreshHandleMaterials()
{
	for (int32 i = 0; i < PointHandles.Num(); ++i)
	{
		UStaticMeshComponent* Handle = PointHandles[i];
		if (!Handle) continue;

		// Hover wins over selection so the point you are about to grab always reads as the target.
		UMaterialInstanceDynamic* Material = MatHandle;
		bool bOnTop = false;
		if (i == HighlightedPoint)
		{
			Material = MatHandleHover;
			bOnTop = true;
		}
		else if (i == SelectedPoint)
		{
			Material = MatHandleSelected;
			bOnTop = true;
		}

		if (Material)
		{
			Handle->SetMaterial(0, Material);
		}

		// Selected and hovered handles render in the foreground depth group, the same pass
		// Unreal's own editor gizmos use, so they draw on top of roads, terrain, walls —
		// everything. Unselected handles use the normal world group and get occluded normally.
		const ESceneDepthPriorityGroup DesiredDPG = bOnTop ? SDPG_Foreground : SDPG_World;
		if (Handle->DepthPriorityGroup != DesiredDPG)
		{
			Handle->DepthPriorityGroup = DesiredDPG;
			Handle->MarkRenderStateDirty();
		}
	}
}

int32 ARoadEditOverlayActor::GetPointIndexFromComponent(const UPrimitiveComponent* Component) const
{
	if (!Component) return INDEX_NONE;

	for (int32 i = 0; i < PointHandles.Num(); ++i)
	{
		if (PointHandles[i] == Component)
		{
			return i;
		}
	}
	return INDEX_NONE;
}
