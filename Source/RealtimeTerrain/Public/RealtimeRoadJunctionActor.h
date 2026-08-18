// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadTypes.h"
#include "RealtimeRoadJunctionActor.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class ARealtimeRoadSplineActor;
class ATerraDyneManager;

/**
 * An intersection where two or more roads meet.
 *
 * The slab is a filleted polygon derived from where the legs' edges actually cross, not a fan of
 * chords between arm corners, and it carries curbs and footpaths around every corner so it reads
 * continuous with the roads that connect to it.
 *
 * A junction is planar by design: real intersections are graded flat. It samples terrain to decide
 * which height that plane sits at, and publishes it through GetLegConnectionTransform so connected
 * roads meet it exactly.
 */
UCLASS(Blueprintable, ClassGroup=(Roads))
class REALTIMETERRAIN_API ARealtimeRoadJunctionActor : public AActor
{
	GENERATED_BODY()

public:
	ARealtimeRoadJunctionActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Junction")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Junction")
	TObjectPtr<UDynamicMeshComponent> JunctionMeshComponent;

	//--- PRESET & LEGS ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Configuration")
	ERoadJunctionType JunctionType = ERoadJunctionType::Cross_4Way;

	/** Default width of connected road channels. A leg may override it with CustomRoadWidth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "200.0"))
	float RoadWidth = 800.0f;

	/**
	 * Radius of the rounded corner between adjacent legs. Clamped per corner against the available
	 * arm length, and against the combined curb and footpath width, which the fillet must exceed
	 * for the roadside band to stay valid.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "50.0"))
	float CornerFilletRadius = 500.0f;

	/** Segments used to tessellate each corner arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "1", ClampMax = "32"))
	int32 FilletSegments = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "0.0"))
	float CurbHeight = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "0.0"))
	float CurbWidth = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "0.0"))
	float FootpathWidth = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "0.0"))
	float FootpathHeight = 15.0f;

	/** Thickness of the slab, matching the connecting roads so the two line up from the side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "2.0"))
	float RoadThickness = 25.0f;

	/** World units to UV units. Match the connecting roads to keep texel density continuous. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_JunctionConfig, Category = "Junction|Dimensions", meta = (ClampMin = "0.00001"))
	float UVTilingScale = 0.002f;

	/** Legs radiating from the centre. Presets fill this in; Custom_N_Way leaves it to the caller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Legs, Category = "Junction|Legs")
	TArray<FRoadJunctionLeg> Legs;

	//--- TERRAIN ---

	/** Sit the junction plane on the terrain rather than at the actor's authored Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Terrain")
	bool bConformToTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Terrain", meta = (ClampMin = "0.0"))
	float TerrainSurfaceOffset = 8.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Junction|Realtime")
	bool bIsHolographicPreview = false;

	//--- MATERIALS ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Materials")
	TObjectPtr<UMaterialInterface> AsphaltMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Materials")
	TObjectPtr<UMaterialInterface> CurbMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Materials")
	TObjectPtr<UMaterialInterface> FootpathMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Materials")
	TObjectPtr<UMaterialInterface> HolographicMaterial;

	//--- REPLICATION HANDLERS ---

	UFUNCTION()
	void OnRep_JunctionConfig();

	UFUNCTION()
	void OnRep_Legs();

	//--- REALTIME APIS ---

	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	void SetIsHolographicPreview(bool bHolographic);

	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	void RebuildJunction();

	/**
	 * Replaces Legs with the layout for the current JunctionType. A no-op for Custom_N_Way, which
	 * is the mode that means "the legs are authored, leave them alone".
	 */
	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	void SyncPresetLegs();

	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	void AddLegRealtime(float AngleDegrees, float ArmLength = 500.0f, float CustomRoadWidth = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	bool FindClosestLeg(const FVector& WorldLocation, int32& OutLegIndex, float& OutDistance) const;

	/**
	 * Socket where a road should meet the given leg: the centre of the arm end, at the junction's
	 * conformed plane height, facing outward.
	 */
	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	bool GetLegConnectionTransform(int32 LegIndex, FTransform& OutTransform) const;

	/** Effective width of a leg, resolving CustomRoadWidth against the junction default. */
	UFUNCTION(BlueprintCallable, Category = "Junction|Realtime")
	float GetLegWidth(int32 LegIndex) const;

protected:
	void ApplyMaterials();

	/** Absolute world height of the junction plane, from terrain when conforming is on. */
	double ResolvePlaneWorldZ() const;

	/**
	 * Builds the counter-clockwise slab boundary in world space, and the sub-chain of that
	 * boundary between each pair of legs that a roadside band should follow.
	 *
	 * OutMaxBandWidth reports the widest roadside band the tightest corner can carry. A band wider
	 * than its corner's fillet radius would fold through the arc's centre, so the caller scales the
	 * curb and footpath to fit rather than letting one cramped corner invert.
	 */
	bool BuildBoundary(
		TArray<FVector>& OutBoundary,
		TArray<TArray<FVector>>& OutCornerChains,
		double& OutMaxBandWidth) const;

	bool SampleTerrainHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const;

private:
	mutable TWeakObjectPtr<ATerraDyneManager> CachedTerraDyneManager;

	/**
	 * Cached by RebuildJunction so leg sockets agree with the mesh that was last built. Recomputed
	 * on every rebuild, which a move triggers through OnConstruction.
	 */
	double ConformedPlaneWorldZ = 0.0;
};
