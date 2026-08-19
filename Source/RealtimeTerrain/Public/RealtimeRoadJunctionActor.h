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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
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

	//--- BOUNDING VOLUME SOLVE ---

	/**
	 * Discover the roads that pass through this junction's bounds and solve its surface from them,
	 * rather than from authored legs.
	 *
	 * This is the workflow the Houdini reference uses: the user places a bounding shape over the
	 * spot they want an intersection, and whatever roads fall inside become its arms. It avoids
	 * needing a crossing heuristic entirely, because placement is the author's decision.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Solve")
	bool bSolveFromNearbyRoads = true;

	/** Radius of the bounding volume, in cm. A road passing within it becomes part of this node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Solve", meta = (ClampMin = "100.0"))
	float BoundsRadius = 1400.0f;

	/**
	 * Radius of the arc rounding each outer corner, in cm.
	 *
	 * The main dial for the junction's look and size: every arm trims back by roughly half its
	 * width plus this, so a larger return sweeps wider and produces a bigger intersection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Solve", meta = (ClampMin = "0.0"))
	float CornerReturnRadius = 300.0f;

	/** Segments per corner arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Solve", meta = (ClampMin = "1", ClampMax = "32"))
	int32 CornerArcSegments = 8;

	/**
	 * Rebuilds Legs and the surface from the roads currently inside the bounds, and writes the
	 * solver's trim distances back onto those roads as junction cuts.
	 *
	 * Deliberately not named to match bSolveFromNearbyRoads: reflection strips the leading b from
	 * a bool property, so a function of that name would collide with the flag in Blueprint and in
	 * Python, and the property would win.
	 *
	 * Returns false when fewer than two arms were found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Junction|Solve")
	bool SolveIntersection();

	/**
	 * True while any junction is mid-solve.
	 *
	 * Solving writes cuts onto the roads it trims, and those roads rebuild in response. Without
	 * this the rebuild would ask its junctions to re-solve and the two would recurse forever.
	 */
	static bool IsAnySolveInProgress() { return bAnySolveInProgress; }

	/** Legs radiating from the centre. Presets fill this in; Custom_N_Way leaves it to the caller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Legs, Category = "Junction|Legs")
	TArray<FRoadJunctionLeg> Legs;

	//--- TERRAIN ---

	/**
	 * Level the ground under the intersection to its plane.
	 *
	 * A junction is a single flat slab by design, so on any slope the ground it spans rises above
	 * it at one end and falls away at the other. Roads solve this by rebasing the terrain beneath
	 * them; without the same treatment the intersection is the one place the landscape still
	 * surfaces through the road, and it is the largest flat piece in the network.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Terrain")
	bool bRebaseTerrain = true;

	/** Ground levelled beyond the junction outline, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Terrain", meta = (ClampMin = "0.0"))
	float PadMargin = 150.0f;

	/** Width of the blend from the levelled area back to natural ground, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Terrain", meta = (ClampMin = "1.0"))
	float PadFalloff = 350.0f;

	/** How far below the surface the ground is levelled, on top of the slab thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Junction|Terrain", meta = (ClampMin = "0.0"))
	float PadDepthBelowSurface = 60.0f;

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

	/** Pushes the junction's footprint to the terrain manager as a pad, and refreshes chunks. */
	void UpdateTerrainPad();

	/** Drops the junction's pad, restoring the natural landform underneath. */
	void ClearTerrainPad();

private:
	mutable TWeakObjectPtr<ATerraDyneManager> CachedTerraDyneManager;

	/**
	 * Cached by RebuildJunction so leg sockets agree with the mesh that was last built. Recomputed
	 * on every rebuild, which a move triggers through OnConstruction.
	 */
	double ConformedPlaneWorldZ = 0.0;

	/**
	 * Surface solved from nearby roads, in world XY. Empty when the junction is running on
	 * authored legs instead.
	 */
	TArray<FVector2D> SolvedBoundary;

	/** Stretches of SolvedBoundary that are roadside rather than a road connection. */
	TArray<TArray<FVector2D>> SolvedRoadsideChains;

	/** Guards the road-rebuild cycle described on IsAnySolveInProgress. */
	static bool bAnySolveInProgress;
};
