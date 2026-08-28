// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Texture2D.h"

using UE::Geometry::FDynamicMesh3;

/**
 * Material slot description extracted from an imported 3D model (e.g. OBJ/MTL).
 */
struct TERRADYNE_API FTerraDyneImportedMaterial
{
	FString MaterialName;
	FLinearColor DiffuseColor = FLinearColor::White;
	FString DiffuseTexturePath;
	TObjectPtr<UTexture2D> DiffuseTexture = nullptr;
};

/**
 * Result data produced when a 3D model file is imported.
 */
struct TERRADYNE_API FTerraDyneImportModelResult
{
	bool bSuccess = false;
	FString ErrorMessage;
	FString ModelName;
	FString SourceFilePath;
	FDynamicMesh3 Mesh;
	TArray<FTerraDyneImportedMaterial> Materials;
	TArray<int32> TriangleMaterialIDs;
	FBox3d BoundingBox;
	int32 VertexCount = 0;
	int32 TriangleCount = 0;
};

/**
 * High-performance, zero-external-dependency runtime 3D model importer supporting OBJ, STL, and PLY formats.
 */
class TERRADYNE_API FTerraDyneModelImporter
{
public:
	/** Imports a 3D model from disk, automatically choosing the format parser from file extension. */
	static FTerraDyneImportModelResult ImportModelFromFile(const FString& FilePath);

	/** Parses Wavefront OBJ (.obj) geometry and accompanying .mtl material definition. */
	static bool ImportOBJ(const FString& FilePath, FTerraDyneImportModelResult& OutResult);

	/** Parses Stereolithography STL (.stl) geometry in both Binary and ASCII variants. */
	static bool ImportSTL(const FString& FilePath, FTerraDyneImportModelResult& OutResult);

	/** Parses Polygon File Format PLY (.ply) geometry in both Binary and ASCII variants. */
	static bool ImportPLY(const FString& FilePath, FTerraDyneImportModelResult& OutResult);

	/** Parses Autodesk FBX (.fbx) geometry, UVs, normals, and materials via runtime ufbx library. */
	static bool ImportFBX(const FString& FilePath, FTerraDyneImportModelResult& OutResult);

	/** Parses glTF 2.0 (.gltf) and Binary glTF (.glb) scenes, primitives, and PBR materials via cgltf library. */
	static bool ImportGLTF(const FString& FilePath, FTerraDyneImportModelResult& OutResult);

	/** Loads an image file (.png, .jpg, .bmp) from disk into a transient UTexture2D. */
	static UTexture2D* LoadTextureFromFile(const FString& FilePath);

	/** Loads an image from a raw memory buffer into a transient UTexture2D. */
	static UTexture2D* LoadTextureFromMemory(const uint8* BufferData, int64 BufferSize, const FString& FormatHint = TEXT("png"));

	/** Returns true if the given extension is a supported 3D model format. */
	static bool IsSupportedExtension(const FString& Extension);

	/** Returns standard file dialog filter string for supported formats. */
	static FString GetSupportedFileFilter();
};
