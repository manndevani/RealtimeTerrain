// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Gizmo/RuntimeGizmoActor.h"
#include "Gizmo/GizmoManipulationMath.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	// How close (world units at the gizmo's default scale, before its own camera-distance scale
	// multiplies in) a ray needs to come to a handle's own geometry to count as a hit -- distinct
	// from ARuntimeGizmoActor::GetHandleReach, which is a handle's own SIZE (an input to the
	// distance functions), not a tolerance on their output.
	constexpr double ClickTolerance = 20.0;
}

void UGizmoManipulationSubsystem::Deinitialize()
{
	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
	}
	Super::Deinitialize();
}

ARuntimeGizmoActor* UGizmoManipulationSubsystem::EnsureGizmoActor()
{
	if (!GizmoActor)
	{
		UWorld* World = GetWorld();
		if (!World) return nullptr;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		GizmoActor = World->SpawnActor<ARuntimeGizmoActor>(ARuntimeGizmoActor::StaticClass(), FTransform::Identity, SpawnParams);
	}
	return GizmoActor;
}

FVector UGizmoManipulationSubsystem::GetPivotLocation() const
{
	if (State.PivotMode == EGizmoPivotMode::Custom)
	{
		return State.CustomPivot;
	}
	if (State.Target.IsValid())
	{
		const AActor* Owner = State.Target->GetOwner();
		return Owner ? Owner->GetActorLocation() : State.Target->GetComponentLocation();
	}
	return FVector::ZeroVector;
}

void UGizmoManipulationSubsystem::UpdateGizmoTransform()
{
	if (!GizmoActor) return;

	GizmoActor->SetActorLocation(GetPivotLocation());

	FQuat Rotation = FQuat::Identity;
	if (State.Space == EGizmoCoordinateSpace::Local && State.Target.IsValid())
	{
		const AActor* Owner = State.Target->GetOwner();
		Rotation = Owner ? Owner->GetActorQuat() : State.Target->GetComponentQuat();
	}
	GizmoActor->SetActorRotation(Rotation);
}

void UGizmoManipulationSubsystem::SetTarget(USceneComponent* NewTarget)
{
	if (!NewTarget)
	{
		ClearTarget();
		return;
	}

	State.Target = NewTarget;
	if (ARuntimeGizmoActor* Gizmo = EnsureGizmoActor())
	{
		Gizmo->SetActorHiddenInGame(false);
		Gizmo->SetActorTickEnabled(true);
		Gizmo->SetMode(State.Mode);
	}
	UpdateGizmoTransform();
}

void UGizmoManipulationSubsystem::ClearTarget()
{
	State.Target = nullptr;
	State.bIsManipulating = false;
	State.ActiveHandle = EGizmoHandleType::None;
	State.HoveredHandle = EGizmoHandleType::None;

	if (GizmoActor)
	{
		GizmoActor->SetActorHiddenInGame(true);
		GizmoActor->SetActorTickEnabled(false);
		GizmoActor->SetHighlightedHandle(EGizmoHandleType::None);
	}
}

void UGizmoManipulationSubsystem::SetMode(EGizmoMode NewMode)
{
	State.Mode = NewMode;
	if (GizmoActor)
	{
		GizmoActor->SetMode(NewMode);
	}
}

void UGizmoManipulationSubsystem::SetCoordinateSpace(EGizmoCoordinateSpace NewSpace)
{
	State.Space = NewSpace;
	UpdateGizmoTransform();
}

void UGizmoManipulationSubsystem::SetPivotMode(EGizmoPivotMode NewPivotMode, const FVector& InCustomPivot)
{
	State.PivotMode = NewPivotMode;
	State.CustomPivot = InCustomPivot;
	UpdateGizmoTransform();
}

EGizmoHandleType UGizmoManipulationSubsystem::FindHoveredHandle(const FGizmoInteractionRay& Ray, const FVector& CameraForward, FVector& OutHitPoint) const
{
	OutHitPoint = FVector::ZeroVector;
	if (!GizmoActor || !State.Target.IsValid())
	{
		return EGizmoHandleType::None;
	}

	TArray<EGizmoHandleType> Candidates;
	switch (State.Mode)
	{
	case EGizmoMode::Translate:
		Candidates = {
			EGizmoHandleType::TranslateX, EGizmoHandleType::TranslateY, EGizmoHandleType::TranslateZ,
			EGizmoHandleType::PlaneXY, EGizmoHandleType::PlaneXZ, EGizmoHandleType::PlaneYZ,
			EGizmoHandleType::Center };
		break;
	case EGizmoMode::Rotate:
		Candidates = { EGizmoHandleType::RotateX, EGizmoHandleType::RotateY, EGizmoHandleType::RotateZ, EGizmoHandleType::RotateScreen };
		break;
	case EGizmoMode::Scale:
		Candidates = { EGizmoHandleType::ScaleX, EGizmoHandleType::ScaleY, EGizmoHandleType::ScaleZ, EGizmoHandleType::ScaleUniform };
		break;
	}

	const FVector Origin = GizmoActor->GetActorLocation();
	const double Tolerance = ClickTolerance * GizmoActor->GetActorScale3D().X;

	EGizmoHandleType Best = EGizmoHandleType::None;
	double BestAlongRay = TNumericLimits<double>::Max();
	FVector BestHitPoint = FVector::ZeroVector;

	for (EGizmoHandleType Handle : Candidates)
	{
		UPrimitiveComponent* Comp = GizmoActor->GetComponentForHandle(Handle);
		if (!Comp)
		{
			continue;
		}

		double Dist = TNumericLimits<double>::Max();
		double AlongRay = TNumericLimits<double>::Max();
		FVector HitPoint = FVector::ZeroVector;
		const double Reach = GizmoActor->GetHandleReach(Handle);
		const FTransform CompXform = Comp->GetComponentTransform();

		switch (Handle)
		{
		case EGizmoHandleType::TranslateX:
		case EGizmoHandleType::TranslateY:
		case EGizmoHandleType::TranslateZ:
		{
			// Translate arrows (Cylinder/Cone) point along their own local Z by default.
			const FVector Axis = CompXform.GetUnitAxis(EAxis::Z);
			Dist = GizmoMath::DistanceRayToSegment(Ray, Origin, Axis, Reach, AlongRay);
			const double T = FMath::Clamp(FVector::DotProduct((Ray.Origin + Ray.Direction * AlongRay) - Origin, Axis), 0.0, Reach);
			HitPoint = Origin + Axis * T;
			break;
		}
		case EGizmoHandleType::ScaleX:
		case EGizmoHandleType::ScaleY:
		case EGizmoHandleType::ScaleZ:
		{
			// The migrated scale-handle mesh's primary axis is local X (see RuntimeGizmoActor.cpp).
			const FVector Axis = CompXform.GetUnitAxis(EAxis::X);
			Dist = GizmoMath::DistanceRayToSegment(Ray, Origin, Axis, Reach, AlongRay);
			const double T = FMath::Clamp(FVector::DotProduct((Ray.Origin + Ray.Direction * AlongRay) - Origin, Axis), 0.0, Reach);
			HitPoint = Origin + Axis * T;
			break;
		}
		case EGizmoHandleType::RotateX:
		case EGizmoHandleType::RotateY:
		case EGizmoHandleType::RotateZ:
		{
			// The migrated ring mesh's normal is also local X (see RuntimeGizmoActor.cpp).
			const FVector Axis = CompXform.GetUnitAxis(EAxis::X);
			Dist = GizmoMath::DistanceRayToRing(Ray, Origin, Axis, Reach, AlongRay);
			HitPoint = Ray.Origin + Ray.Direction * AlongRay;
			break;
		}
		case EGizmoHandleType::RotateScreen:
		{
			const FVector Axis = CameraForward.IsNearlyZero() ? FVector::UpVector : CameraForward.GetSafeNormal();
			Dist = GizmoMath::DistanceRayToRing(Ray, Origin, Axis, Reach, AlongRay);
			HitPoint = Ray.Origin + Ray.Direction * AlongRay;
			break;
		}
		case EGizmoHandleType::PlaneXY:
		case EGizmoHandleType::PlaneXZ:
		case EGizmoHandleType::PlaneYZ:
		{
			const FVector AxisU = CompXform.GetUnitAxis(EAxis::X);
			const FVector AxisV = CompXform.GetUnitAxis(EAxis::Y);
			Dist = GizmoMath::DistanceRayToQuad(Ray, CompXform.GetLocation(), AxisU, AxisV, Reach, AlongRay);
			HitPoint = Ray.Origin + Ray.Direction * AlongRay;
			break;
		}
		case EGizmoHandleType::Center:
		case EGizmoHandleType::ScaleUniform:
		{
			Dist = GizmoMath::DistanceRayToPoint(Ray, CompXform.GetLocation(), AlongRay);
			HitPoint = CompXform.GetLocation();
			break;
		}
		default:
			break;
		}

		if (Dist <= Tolerance && AlongRay < BestAlongRay)
		{
			Best = Handle;
			BestAlongRay = AlongRay;
			BestHitPoint = HitPoint;
		}
	}

	OutHitPoint = BestHitPoint;
	return Best;
}

EGizmoHandleType UGizmoManipulationSubsystem::UpdateHover(const FGizmoInteractionRay& Ray, const FVector& CameraForward)
{
	if (State.bIsManipulating || !State.Target.IsValid())
	{
		return EGizmoHandleType::None;
	}

	FVector HitPoint;
	const EGizmoHandleType Handle = FindHoveredHandle(Ray, CameraForward, HitPoint);
	State.HoveredHandle = Handle;
	if (GizmoActor)
	{
		GizmoActor->SetHighlightedHandle(Handle);
	}
	return Handle;
}

EGizmoHandleType UGizmoManipulationSubsystem::BeginManipulation(const FGizmoInteractionRay& Ray, const FVector& CameraForward)
{
	if (!State.Target.IsValid())
	{
		return EGizmoHandleType::None;
	}

	FVector HitPoint;
	const EGizmoHandleType Handle = FindHoveredHandle(Ray, CameraForward, HitPoint);
	if (Handle == EGizmoHandleType::None)
	{
		return EGizmoHandleType::None;
	}

	AActor* TargetActor = State.Target->GetOwner();
	if (!TargetActor)
	{
		return EGizmoHandleType::None;
	}

	State.ActiveHandle = Handle;
	State.bIsManipulating = true;
	State.InitialTransform = TargetActor->GetActorTransform();
	State.InitialRay = Ray;
	State.InitialIntersection = HitPoint;

	if (GizmoActor)
	{
		GizmoActor->SetHighlightedHandle(Handle);
	}

	OnManipulationBegin.Broadcast(State.Target.Get());
	return Handle;
}

void UGizmoManipulationSubsystem::UpdateManipulation(const FGizmoInteractionRay& Ray, const FVector& CameraForward)
{
	if (!State.bIsManipulating || !State.Target.IsValid())
	{
		return;
	}

	AActor* TargetActor = State.Target->GetOwner();
	if (!TargetActor)
	{
		return;
	}

	// The gizmo's own pivot/rotation at drag START, held fixed for the whole drag -- every delta is
	// computed relative to this, not the target's current (already-modified) transform, so rounding
	// error can never accumulate across a long drag.
	const FVector Origin = State.InitialTransform.GetLocation();
	const FQuat TargetRotation = State.InitialTransform.GetRotation();

	FVector CurrentIntersection;
	bool bGotIntersection = false;

	switch (State.Mode)
	{
	case EGizmoMode::Translate:
	{
		const FPlane DragPlane = GizmoMath::ResolveTranslateDragPlane(State.ActiveHandle, State.Space, TargetRotation, Origin, Ray.Direction, CameraForward);
		bGotIntersection = GizmoMath::IntersectRayWithPlane(Ray, DragPlane, CurrentIntersection);
		break;
	}
	case EGizmoMode::Rotate:
	{
		FVector Axis;
		if (State.ActiveHandle == EGizmoHandleType::RotateScreen)
		{
			Axis = CameraForward.IsNearlyZero() ? FVector::UpVector : CameraForward.GetSafeNormal();
		}
		else
		{
			Axis = GizmoMath::ResolveAxisDirection(State.ActiveHandle, State.Space, TargetRotation);
		}
		const FPlane RingPlane(Origin, Axis);
		bGotIntersection = GizmoMath::IntersectRayWithPlane(Ray, RingPlane, CurrentIntersection);
		break;
	}
	case EGizmoMode::Scale:
	{
		if (State.ActiveHandle == EGizmoHandleType::ScaleUniform)
		{
			const FVector ViewNormal = CameraForward.IsNearlyZero() ? FVector::UpVector : CameraForward.GetSafeNormal();
			bGotIntersection = GizmoMath::IntersectRayWithPlane(Ray, FPlane(Origin, ViewNormal), CurrentIntersection);
		}
		else
		{
			const FVector Axis = GizmoMath::ResolveAxisDirection(State.ActiveHandle, State.Space, TargetRotation);
			const FPlane DragPlane = GizmoMath::ResolveAxisDragPlane(Axis, Origin, Ray.Direction, CameraForward);
			bGotIntersection = GizmoMath::IntersectRayWithPlane(Ray, DragPlane, CurrentIntersection);
		}
		break;
	}
	}

	if (!bGotIntersection)
	{
		return;
	}

	FTransform NewTransform = State.InitialTransform;

	switch (State.Mode)
	{
	case EGizmoMode::Translate:
	{
		FVector Delta = GizmoMath::ComputeTranslateDelta(State.ActiveHandle, State.Space, TargetRotation, State.InitialIntersection, CurrentIntersection);
		if (State.bSnappingEnabled)
		{
			Delta = GizmoMath::SnapVector(Delta, State.TranslateSnapSize);
		}
		NewTransform.SetLocation(State.InitialTransform.GetLocation() + Delta);
		break;
	}
	case EGizmoMode::Rotate:
	{
		FVector Axis;
		if (State.ActiveHandle == EGizmoHandleType::RotateScreen)
		{
			Axis = CameraForward.IsNearlyZero() ? FVector::UpVector : CameraForward.GetSafeNormal();
		}
		else
		{
			Axis = GizmoMath::ResolveAxisDirection(State.ActiveHandle, State.Space, TargetRotation);
		}
		double Angle = GizmoMath::ComputeRotationAngle(Axis, Origin, State.InitialIntersection, CurrentIntersection);
		if (State.bSnappingEnabled)
		{
			Angle = FMath::DegreesToRadians(GizmoMath::SnapValue(FMath::RadiansToDegrees(Angle), State.RotateSnapDegrees));
		}
		const FQuat DeltaQuat(Axis, Angle);
		NewTransform.SetRotation(DeltaQuat * TargetRotation);
		break;
	}
	case EGizmoMode::Scale:
	{
		FVector Ratio = GizmoMath::ComputeScaleDelta(State.ActiveHandle, State.Space, TargetRotation, Origin, State.InitialIntersection, CurrentIntersection);
		if (State.bSnappingEnabled)
		{
			Ratio = GizmoMath::SnapVector(Ratio, State.ScaleSnapIncrement);
		}
		NewTransform.SetScale3D(State.InitialTransform.GetScale3D() * Ratio);
		break;
	}
	}

	TargetActor->SetActorTransform(NewTransform);

	// The gizmo itself only follows the target for Translate/Rotate -- scaling the object shouldn't
	// also grow/shrink the gizmo, matching how a real DCC tool's gizmo behaves.
	if (State.Mode != EGizmoMode::Scale)
	{
		UpdateGizmoTransform();
	}
}

void UGizmoManipulationSubsystem::EndManipulation()
{
	if (!State.bIsManipulating)
	{
		return;
	}

	USceneComponent* Target = State.Target.Get();
	State.bIsManipulating = false;
	State.ActiveHandle = EGizmoHandleType::None;

	OnManipulationEnd.Broadcast(Target);
}

void UGizmoManipulationSubsystem::CancelManipulation()
{
	if (!State.bIsManipulating)
	{
		return;
	}

	USceneComponent* Target = State.Target.Get();
	if (AActor* TargetActor = Target ? Target->GetOwner() : nullptr)
	{
		TargetActor->SetActorTransform(State.InitialTransform);
	}

	State.bIsManipulating = false;
	State.ActiveHandle = EGizmoHandleType::None;

	UpdateGizmoTransform();
	OnManipulationCancel.Broadcast(Target);
}
