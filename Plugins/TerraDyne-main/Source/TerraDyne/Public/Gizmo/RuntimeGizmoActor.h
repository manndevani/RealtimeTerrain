// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Gizmo/GizmoTypes.h"
#include "RuntimeGizmoActor.generated.h"

/**
 * The visual representation of the runtime transform gizmo -- translate/rotate/scale handles, all
 * spawned once and toggled by mode rather than rebuilt. Every handle's identity is tracked
 * explicitly via HandleTypeByComponent, built once in the constructor -- GetHandleType/
 * GetComponentForHandle never infer identity from a mesh or material, so recoloring a handle for
 * hover/active/disabled feedback can never be mistaken for a change in what it IS.
 *
 * Collision is QueryOnly on every handle (consistent with the project's existing gizmo), but the
 * real hit-testing this system relies on is the dedicated math in GizmoManipulationMath.h/.cpp
 * (Phase 3 wires it against these handles' live transforms) -- collision here is a secondary/visual
 * aid, not the precision hit-test path, which is why the migrated SM_RotationHandle's own custom
 * collision mesh (SM_RotationHandleCollision, from the source repo) was deliberately not migrated.
 */
UCLASS()
class TERRADYNE_API ARuntimeGizmoActor : public AActor
{
	GENERATED_BODY()

public:
	ARuntimeGizmoActor();

	virtual void Tick(float DeltaTime) override;
	virtual void PostInitializeComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Shows and enables collision on exactly the handles belonging to NewMode; hides and disables collision on every other handle. */
	void SetMode(EGizmoMode NewMode);
	EGizmoMode GetMode() const { return CurrentMode; }

	/** Explicit identity lookup -- never inferred from mesh/material. Returns None for an unrecognized component. */
	EGizmoHandleType GetHandleType(const UPrimitiveComponent* Component) const;

	/** Reverse lookup: the representative component for a handle type (the tip, for the two-piece translate arrows). */
	UPrimitiveComponent* GetComponentForHandle(EGizmoHandleType Handle) const;

	void SetHighlightedHandle(EGizmoHandleType Handle);
	EGizmoHandleType GetHighlightedHandle() const { return CurrentHighlight; }

	/**
	 * Sets the selected target's bounding radius so Tick() can grow the gizmo's screen-space scale
	 * enough that its handles reach past a large object's own geometry instead of rendering buried
	 * inside it. Purely additive to the normal camera-distance scale -- never shrinks it.
	 */
	void SetTargetBoundingExtent(float ExtentRadius) { TargetBoundingRadius = ExtentRadius; }

	/**
	 * A handle's own size, in current world units (already multiplied by this actor's camera-
	 * distance scale factor) -- segment length for a translate/scale axis, ring radius for a
	 * rotation handle, quad half-size for a plane handle, or a fixed click radius for a point-like
	 * handle (Center/ScaleUniform). This is an INPUT to GizmoManipulationMath's distance functions
	 * (the handle's own geometry), not a click-tolerance threshold on their output -- callers doing
	 * hit-testing still need their own separate small tolerance to compare the returned distance
	 * against. Single source of truth: reads the same constants the constructor places handles
	 * with, so hit-testing geometry can never drift out of sync with what's actually rendered.
	 */
	double GetHandleReach(EGizmoHandleType Handle) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	TObjectPtr<USceneComponent> SceneRoot;

	// Translate: axis arrows (shaft + tip), plane quads, screen-space centre.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshShaftX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshTipX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshShaftY;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshTipY;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshShaftZ;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshTipZ;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshPlaneXY;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshPlaneXZ;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshPlaneYZ;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Translate")
	TObjectPtr<UStaticMeshComponent> MeshCenter;

	// Rotate: one ring per axis, plus a free screen-space ring.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Rotate")
	TObjectPtr<UStaticMeshComponent> MeshRotateX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Rotate")
	TObjectPtr<UStaticMeshComponent> MeshRotateY;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Rotate")
	TObjectPtr<UStaticMeshComponent> MeshRotateZ;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Rotate")
	TObjectPtr<UStaticMeshComponent> MeshRotateScreen;

	// Scale: one handle per axis, plus a uniform-scale handle at the origin.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Scale")
	TObjectPtr<UStaticMeshComponent> MeshScaleX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Scale")
	TObjectPtr<UStaticMeshComponent> MeshScaleY;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Scale")
	TObjectPtr<UStaticMeshComponent> MeshScaleZ;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo|Scale")
	TObjectPtr<UStaticMeshComponent> MeshScaleUniform;

private:
	/** See TerraDyneGizmoActor.cpp's identical field for why this is non-Transient (cooker reachability). */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GizmoBaseMaterial;

	void EnsureDynamicMaterials();
	void ApplyMaterialsToComponents();
	static void ApplyGizmoColor(UMaterialInstanceDynamic* DMI, const FLinearColor& InColor);
	void BuildHandleTypeMap();

	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstX;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstY;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstZ;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstNeutral;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstHovered;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstRotX;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstRotY;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstRotZ;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstRotHovered;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstScreenRot;
	UPROPERTY() TObjectPtr<UMaterialInterface> MatInstScreenRotHovered;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatX;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatY;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatZ;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaneXY;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaneXZ;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatPlaneYZ;
	/** Shared by every "no single axis" handle (Center, RotateScreen, ScaleUniform) -- never more than one is visible at once, since they belong to three different, mutually exclusive modes. */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MatNeutral;

	EGizmoMode CurrentMode = EGizmoMode::Translate;
	EGizmoHandleType CurrentHighlight = EGizmoHandleType::None;
	float TargetBoundingRadius = 0.0f;

	/** Built once in BuildHandleTypeMap; every handle component (including both pieces of a translate arrow) is a key. */
	TMap<TWeakObjectPtr<const UPrimitiveComponent>, EGizmoHandleType> HandleTypeByComponent;

	/** Reverse of the above, one representative component per handle type. */
	TMap<EGizmoHandleType, TWeakObjectPtr<UPrimitiveComponent>> ComponentByHandleType;
};
