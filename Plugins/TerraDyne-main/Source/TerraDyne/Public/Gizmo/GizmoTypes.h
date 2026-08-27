// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GizmoTypes.generated.h"

/**
 * Every interactive part of the gizmo has an explicit, named type -- selection/hover state is
 * always driven by which UPrimitiveComponent was hit, resolved through this enum, never inferred
 * from a mesh or material identity (a handle's material changes freely for hover/active/disabled
 * feedback without that ever being read back as "what got clicked").
 */
UENUM(BlueprintType)
enum class EGizmoHandleType : uint8
{
	None,
	TranslateX, TranslateY, TranslateZ,
	PlaneXY, PlaneXZ, PlaneYZ,
	/** Screen-space (view-plane) translate -- moves freely in the plane facing the camera. */
	Center,
	RotateX, RotateY, RotateZ,
	/** Free rotation around the camera's own forward axis, independent of the target's own axes. */
	RotateScreen,
	ScaleX, ScaleY, ScaleZ,
	ScaleUniform,
};

UENUM(BlueprintType)
enum class EGizmoMode : uint8
{
	Translate,
	Rotate,
	Scale,
};

UENUM(BlueprintType)
enum class EGizmoCoordinateSpace : uint8
{
	/** Handle axes are always world X/Y/Z, regardless of the target's own rotation. */
	World,
	/** Handle axes follow the target's own rotation. */
	Local,
};

UENUM(BlueprintType)
enum class EGizmoPivotMode : uint8
{
	/** The gizmo sits at, and manipulates around, the target's own origin. */
	ObjectOrigin,
	/** The gizmo sits at, and manipulates around, an explicitly set world point. */
	Custom,
};

/**
 * A single interaction ray, device-agnostic on purpose: mouse deprojection produces one today; a
 * future VR controller's aim transform could produce one without the manipulation math ever
 * knowing which device it came from (Input Device -> Interaction Ray -> hit test -> manipulation).
 */
USTRUCT(BlueprintType)
struct FGizmoInteractionRay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gizmo)
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gizmo)
	FVector Direction = FVector::ForwardVector;
};

/**
 * The live state of one gizmo manipulation session -- owned by UGizmoManipulationSubsystem, but
 * kept as a plain struct (not a UObject) since nothing here needs its own GC root; it's carried as
 * a member of whatever UObject does own it.
 *
 * InitialTransform/InitialRay are captured exactly once, at BeginManipulation, and every delta for
 * the rest of that drag is computed relative to them -- never compounded onto the previous frame's
 * already-modified transform, which is what avoids floating-point drift over a long drag.
 */
USTRUCT()
struct FGizmoManipulationState
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<USceneComponent> Target;

	UPROPERTY()
	EGizmoMode Mode = EGizmoMode::Translate;

	UPROPERTY()
	EGizmoCoordinateSpace Space = EGizmoCoordinateSpace::World;

	UPROPERTY()
	EGizmoPivotMode PivotMode = EGizmoPivotMode::ObjectOrigin;

	UPROPERTY()
	FVector CustomPivot = FVector::ZeroVector;

	UPROPERTY()
	bool bIsManipulating = false;

	UPROPERTY()
	EGizmoHandleType ActiveHandle = EGizmoHandleType::None;

	UPROPERTY()
	EGizmoHandleType HoveredHandle = EGizmoHandleType::None;

	UPROPERTY()
	FTransform InitialTransform;

	UPROPERTY()
	FGizmoInteractionRay InitialRay;

	/** World-space point on the active handle's constraint (line/plane/ring) at drag start. */
	UPROPERTY()
	FVector InitialIntersection = FVector::ZeroVector;

	UPROPERTY()
	bool bSnappingEnabled = false;

	/** Grid size (cm) translate deltas snap to when snapping is active. */
	UPROPERTY()
	float TranslateSnapSize = 100.0f;

	/** Angle increment (degrees) rotation deltas snap to when snapping is active. */
	UPROPERTY()
	float RotateSnapDegrees = 5.0f;

	/** Increment scale ratios snap to when snapping is active. */
	UPROPERTY()
	float ScaleSnapIncrement = 0.1f;
};
