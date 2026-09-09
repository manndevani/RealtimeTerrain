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
class GRT_API FGRTDEMMeshBuilder
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
		float ZScale,
		FVector ChunkLocation = FVector::ZeroVector,
		FVector2D GlobalFootprintMin = FVector2D::ZeroVector,
		FVector2D GlobalFootprintMax = FVector2D::ZeroVector,
		const TArray<FColor>& OverlayPixels = TArray<FColor>(),
		int32 OverlayWidth = 0,
		int32 OverlayHeight = 0);

	/**
	 * Updates an existing DynamicMeshComponent with DEM geometry and vertex colors efficiently.
	 */
	static void UpdateComponentMesh(
		UDynamicMeshComponent* MeshComp,
		const TArray<float>& HeightBuffer,
		int32 Resolution,
		float WorldSize,
		float ZScale,
		bool bComputeCollision = false,
		FVector ChunkLocation = FVector::ZeroVector,
		FVector2D GlobalFootprintMin = FVector2D::ZeroVector,
		FVector2D GlobalFootprintMax = FVector2D::ZeroVector,
		const TArray<FColor>& OverlayPixels = TArray<FColor>(),
		int32 OverlayWidth = 0,
		int32 OverlayHeight = 0);

	/**
	 * Updates only the vertex color overlay on an existing mesh in place in microseconds.
	 */
	static void UpdateDEMVertexColors(
		UE::Geometry::FDynamicMesh3& TargetMesh,
		const TArray<float>& HeightBuffer,
		int32 Resolution,
		float WorldSize,
		FVector ChunkLocation,
		FVector2D GlobalFootprintMin,
		FVector2D GlobalFootprintMax,
		const TArray<FColor>& OverlayPixels = TArray<FColor>(),
		int32 OverlayWidth = 0,
		int32 OverlayHeight = 0);
};
