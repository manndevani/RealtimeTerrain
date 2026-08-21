// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

class UDynamicMeshComponent;

/**
 * High-performance, lightweight DEM mesh builder.
 * Directly constructs optimized FDynamicMesh3 meshes from elevation rasters
 * without procedural noise overhead, heavy textures, or unwanted material instance binds.
 */
class TERRADYNE_API FTerraDyneDEMMeshBuilder
{
public:
	/**
	 * Builds a high-speed terrain mesh into TargetMesh from a raw height buffer.
	 *
	 * @param TargetMesh Dynamic mesh receiving the geometry.
	 * @param HeightBuffer Normalized 0-1 height values (size: Resolution * Resolution).
	 * @param Resolution Number of vertices per dimension.
	 * @param WorldSize Width/Height in Unreal units.
	 * @param ZScale Vertical height scale in Unreal units.
	 */
	static void BuildDEMMesh(
		UE::Geometry::FDynamicMesh3& TargetMesh,
		const TArray<float>& HeightBuffer,
		int32 Resolution,
		float WorldSize,
		float ZScale);

	/**
	 * Updates an existing DynamicMeshComponent with DEM geometry efficiently.
	 */
	static void UpdateComponentMesh(
		UDynamicMeshComponent* MeshComp,
		const TArray<float>& HeightBuffer,
		int32 Resolution,
		float WorldSize,
		float ZScale,
		bool bComputeCollision = false);
};
