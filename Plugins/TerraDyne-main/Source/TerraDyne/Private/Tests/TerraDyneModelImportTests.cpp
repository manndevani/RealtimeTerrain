// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IO/TerraDyneModelImporter.h"
#include "Core/TerraDyneAssetLibrarySubsystem.h"
#include "Building/TerraDynePlacedModelActor.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// -----------------------------------------------------------------------------
// Test 1: OBJ Import
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportOBJTest, "TerraDyne.ModelImport.OBJ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportOBJTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	const FString ObjPath = FPaths::Combine(TestDir, TEXT("CubeTest.obj"));
	const FString MtlPath = FPaths::Combine(TestDir, TEXT("CubeTest.mtl"));

	const FString MtlContent =
		TEXT("newmtl TestMat\n")
		TEXT("Kd 0.8 0.2 0.2\n");

	const FString ObjContent =
		TEXT("mtllib CubeTest.mtl\n")
		TEXT("v 0.0 0.0 0.0\n")
		TEXT("v 100.0 0.0 0.0\n")
		TEXT("v 100.0 100.0 0.0\n")
		TEXT("v 0.0 100.0 0.0\n")
		TEXT("v 0.0 0.0 100.0\n")
		TEXT("v 100.0 0.0 100.0\n")
		TEXT("v 100.0 100.0 100.0\n")
		TEXT("v 0.0 100.0 100.0\n")
		TEXT("vt 0.0 0.0\n")
		TEXT("vt 1.0 0.0\n")
		TEXT("vt 1.0 1.0\n")
		TEXT("vt 0.0 1.0\n")
		TEXT("vn 0.0 0.0 1.0\n")
		TEXT("usemtl TestMat\n")
		TEXT("f 1/1/1 2/2/1 3/3/1 4/4/1\n")
		TEXT("f 5/1/1 6/2/1 7/3/1 8/4/1\n");

	FFileHelper::SaveStringToFile(MtlContent, *MtlPath);
	FFileHelper::SaveStringToFile(ObjContent, *ObjPath);

	FTerraDyneImportModelResult Result = FTerraDyneModelImporter::ImportModelFromFile(ObjPath);
	TestTrue(TEXT("OBJ Import should succeed"), Result.bSuccess);
	TestEqual(TEXT("OBJ ModelName should match base filename"), Result.ModelName, TEXT("CubeTest"));
	TestTrue(TEXT("OBJ TriangleCount should be 4 (2 quads triangulated)"), Result.TriangleCount == 4);
	TestTrue(TEXT("OBJ VertexCount should be 8"), Result.VertexCount == 8);
	TestTrue(TEXT("OBJ Materials should contain TestMat"), Result.Materials.Num() >= 2);

	return true;
}

// -----------------------------------------------------------------------------
// Test 2: STL Import (ASCII & Binary)
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportSTLTest, "TerraDyne.ModelImport.STL",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportSTLTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	// ASCII STL Test
	const FString AsciiStlPath = FPaths::Combine(TestDir, TEXT("TriangleAscii.stl"));
	const FString AsciiStlContent =
		TEXT("solid test\n")
		TEXT("facet normal 0 0 1\n")
		TEXT("  outer loop\n")
		TEXT("    vertex 0 0 0\n")
		TEXT("    vertex 100 0 0\n")
		TEXT("    vertex 0 100 0\n")
		TEXT("  endloop\n")
		TEXT("endfacet\n")
		TEXT("endsolid test\n");

	FFileHelper::SaveStringToFile(AsciiStlContent, *AsciiStlPath);

	FTerraDyneImportModelResult AsciiResult = FTerraDyneModelImporter::ImportModelFromFile(AsciiStlPath);
	TestTrue(TEXT("ASCII STL Import should succeed"), AsciiResult.bSuccess);
	TestEqual(TEXT("ASCII STL TriangleCount should be 1"), AsciiResult.TriangleCount, 1);
	TestEqual(TEXT("ASCII STL VertexCount should be 3"), AsciiResult.VertexCount, 3);

	// Binary STL Test
	const FString BinaryStlPath = FPaths::Combine(TestDir, TEXT("TriangleBinary.stl"));
	TArray<uint8> BinData;
	BinData.SetNumZeroed(84 + 50);
	uint32 NumTris = 1;
	FMemory::Memcpy(BinData.GetData() + 80, &NumTris, sizeof(uint32));

	float TriFloats[12] = {
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 0.0f,
		50.0f, 0.0f, 0.0f,
		0.0f, 50.0f, 0.0f
	};
	FMemory::Memcpy(BinData.GetData() + 84, TriFloats, sizeof(TriFloats));
	FFileHelper::SaveArrayToFile(BinData, *BinaryStlPath);

	FTerraDyneImportModelResult BinaryResult = FTerraDyneModelImporter::ImportModelFromFile(BinaryStlPath);
	TestTrue(TEXT("Binary STL Import should succeed"), BinaryResult.bSuccess);
	TestEqual(TEXT("Binary STL TriangleCount should be 1"), BinaryResult.TriangleCount, 1);
	TestEqual(TEXT("Binary STL VertexCount should be 3"), BinaryResult.VertexCount, 3);

	return true;
}

// -----------------------------------------------------------------------------
// Test 3: PLY Import
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportPLYTest, "TerraDyne.ModelImport.PLY",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportPLYTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	const FString PlyPath = FPaths::Combine(TestDir, TEXT("TestMesh.ply"));
	const FString PlyContent =
		TEXT("ply\n")
		TEXT("format ascii 1.0\n")
		TEXT("element vertex 4\n")
		TEXT("property float x\n")
		TEXT("property float y\n")
		TEXT("property float z\n")
		TEXT("element face 2\n")
		TEXT("property list uchar int vertex_indices\n")
		TEXT("end_header\n")
		TEXT("0.0 0.0 0.0\n")
		TEXT("100.0 0.0 0.0\n")
		TEXT("100.0 100.0 0.0\n")
		TEXT("0.0 100.0 0.0\n")
		TEXT("3 0 1 2\n")
		TEXT("3 0 2 3\n");

	FFileHelper::SaveStringToFile(PlyContent, *PlyPath);

	FTerraDyneImportModelResult Result = FTerraDyneModelImporter::ImportModelFromFile(PlyPath);
	TestTrue(TEXT("PLY Import should succeed"), Result.bSuccess);
	TestEqual(TEXT("PLY TriangleCount should be 2"), Result.TriangleCount, 2);
	TestEqual(TEXT("PLY VertexCount should be 4"), Result.VertexCount, 4);

	return true;
}

// -----------------------------------------------------------------------------
// Test 4: FBX Import (via ufbx)
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportFBXTest, "TerraDyne.ModelImport.FBX",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportFBXTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	const FString FbxPath = FPaths::Combine(TestDir, TEXT("TestTriangle.fbx"));
	const FString FbxContent =
		TEXT("; FBX 7.4.0 project file\n")
		TEXT("FBXHeaderExtension: {\n")
		TEXT("    FBXHeaderVersion: 1003\n")
		TEXT("    FBXVersion: 7400\n")
		TEXT("}\n")
		TEXT("Definitions: {\n")
		TEXT("    Count: 2\n")
		TEXT("    ObjectType: \"Model\" { Count: 1 }\n")
		TEXT("    ObjectType: \"Geometry\" { Count: 1 }\n")
		TEXT("}\n")
		TEXT("Objects: {\n")
		TEXT("    Geometry: 1000, \"Geometry::Mesh\", \"Mesh\" {\n")
		TEXT("        Vertices: *9 {\n")
		TEXT("            a: 0,0,0,10,0,0,0,10,0\n")
		TEXT("        }\n")
		TEXT("        PolygonVertexIndex: *3 {\n")
		TEXT("            a: 0,1,-3\n")
		TEXT("        }\n")
		TEXT("    }\n")
		TEXT("    Model: 2000, \"Model::TestMesh\", \"Mesh\" {\n")
		TEXT("        Version: 232\n")
		TEXT("    }\n")
		TEXT("}\n")
		TEXT("Connections: {\n")
		TEXT("    C: \"OO\",1000,2000\n")
		TEXT("    C: \"OO\",2000,0\n")
		TEXT("}\n");

	FFileHelper::SaveStringToFile(FbxContent, *FbxPath);

	FTerraDyneImportModelResult Result = FTerraDyneModelImporter::ImportModelFromFile(FbxPath);
	TestTrue(TEXT("FBX Import should succeed"), Result.bSuccess);
	TestTrue(TEXT("FBX TriangleCount should be >= 1"), Result.TriangleCount >= 1);
	TestTrue(TEXT("FBX VertexCount should be >= 3"), Result.VertexCount >= 3);

	return true;
}

// -----------------------------------------------------------------------------
// Test 5: glTF 2.0 & GLB Import (via cgltf)
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportGLTFTest, "TerraDyne.ModelImport.GLTF",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportGLTFTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	// 1. glTF Test (.gltf + .bin)
	const FString GltfPath = FPaths::Combine(TestDir, TEXT("Triangle.gltf"));
	const FString BinPath = FPaths::Combine(TestDir, TEXT("Triangle.bin"));

	// 3 vertices (float3 * 3 = 36 bytes) + 3 indices (uint16 * 3 = 6 bytes) -> total 42 bytes
	TArray<uint8> BinBytes;
	BinBytes.SetNumZeroed(42);
	float Positions[9] = { 0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f,  0.0f, 10.0f, 0.0f };
	uint16 Indices[3] = { 0, 1, 2 };
	FMemory::Memcpy(BinBytes.GetData(), Positions, sizeof(Positions));
	FMemory::Memcpy(BinBytes.GetData() + 36, Indices, sizeof(Indices));
	FFileHelper::SaveArrayToFile(BinBytes, *BinPath);

	const FString GltfContent =
		TEXT("{\n")
		TEXT("  \"asset\": { \"version\": \"2.0\" },\n")
		TEXT("  \"scenes\": [ { \"nodes\": [ 0 ] } ],\n")
		TEXT("  \"nodes\": [ { \"mesh\": 0 } ],\n")
		TEXT("  \"meshes\": [ { \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, \"indices\": 1 } ] } ],\n")
		TEXT("  \"buffers\": [ { \"uri\": \"Triangle.bin\", \"byteLength\": 42 } ],\n")
		TEXT("  \"bufferViews\": [\n")
		TEXT("    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n")
		TEXT("    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 6, \"target\": 34963 }\n")
		TEXT("  ],\n")
		TEXT("  \"accessors\": [\n")
		TEXT("    { \"bufferView\": 0, \"byteOffset\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"max\": [10.0, 10.0, 0.0], \"min\": [0.0, 0.0, 0.0] },\n")
		TEXT("    { \"bufferView\": 1, \"byteOffset\": 0, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\", \"max\": [2], \"min\": [0] }\n")
		TEXT("  ]\n")
		TEXT("}\n");

	FFileHelper::SaveStringToFile(GltfContent, *GltfPath);

	FTerraDyneImportModelResult GltfResult = FTerraDyneModelImporter::ImportModelFromFile(GltfPath);
	TestTrue(TEXT("glTF Import should succeed"), GltfResult.bSuccess);
	TestEqual(TEXT("glTF TriangleCount should be 1"), GltfResult.TriangleCount, 1);
	TestEqual(TEXT("glTF VertexCount should be 3"), GltfResult.VertexCount, 3);

	// 2. Binary GLB Test
	const FString GlbPath = FPaths::Combine(TestDir, TEXT("TriangleBinary.glb"));
	FString JsonChunkStr =
		TEXT("{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],")
		TEXT("\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],")
		TEXT("\"buffers\":[{\"byteLength\":42}],")
		TEXT("\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],")
		TEXT("\"accessors\":[{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},")
		TEXT("{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}");

	FTCHARToUTF8 JsonUtf8(*JsonChunkStr);
	int32 JsonLen = JsonUtf8.Length();
	int32 JsonPaddedLen = (JsonLen + 3) & ~3;

	int32 BinLen = BinBytes.Num();
	int32 BinPaddedLen = (BinLen + 3) & ~3;

	int32 TotalGlbSize = 12 + 8 + JsonPaddedLen + 8 + BinPaddedLen;
	TArray<uint8> GlbData;
	GlbData.SetNumZeroed(TotalGlbSize);

	uint8* Ptr = GlbData.GetData();

	// 12-byte header
	uint32 Magic = 0x46546C67; // 'glTF'
	uint32 Version = 2;
	uint32 TotalLen = TotalGlbSize;
	FMemory::Memcpy(Ptr + 0, &Magic, 4);
	FMemory::Memcpy(Ptr + 4, &Version, 4);
	FMemory::Memcpy(Ptr + 8, &TotalLen, 4);

	// JSON Chunk
	uint32 JsonChunkLen = JsonPaddedLen;
	uint32 JsonChunkType = 0x4E4F534A; // 'JSON'
	FMemory::Memcpy(Ptr + 12, &JsonChunkLen, 4);
	FMemory::Memcpy(Ptr + 16, &JsonChunkType, 4);
	FMemory::Memset(Ptr + 20, 0x20, JsonPaddedLen); // pad with spaces
	FMemory::Memcpy(Ptr + 20, JsonUtf8.Get(), JsonLen);

	// BIN Chunk
	int32 BinOffset = 20 + JsonPaddedLen;
	uint32 BinChunkLen = BinPaddedLen;
	uint32 BinChunkType = 0x004E4942; // 'BIN\0'
	FMemory::Memcpy(Ptr + BinOffset + 0, &BinChunkLen, 4);
	FMemory::Memcpy(Ptr + BinOffset + 4, &BinChunkType, 4);
	FMemory::Memcpy(Ptr + BinOffset + 8, BinBytes.GetData(), BinLen);

	FFileHelper::SaveArrayToFile(GlbData, *GlbPath);

	FTerraDyneImportModelResult GlbResult = FTerraDyneModelImporter::ImportModelFromFile(GlbPath);
	TestTrue(TEXT("GLB Import should succeed"), GlbResult.bSuccess);
	TestEqual(TEXT("GLB TriangleCount should be 1"), GlbResult.TriangleCount, 1);
	TestEqual(TEXT("GLB VertexCount should be 3"), GlbResult.VertexCount, 3);

	return true;
}

// -----------------------------------------------------------------------------
// Test 6: Asset Library Subsystem
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneAssetLibraryTest, "TerraDyne.ModelImport.AssetLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneAssetLibraryTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>();
	TestNotNull(TEXT("UTerraDyneAssetLibrarySubsystem must be initialized"), AssetLib);
	if (!AssetLib)
	{
		return false;
	}

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("LibraryTest.obj"));
	const FString ObjContent =
		TEXT("v 0.0 0.0 0.0\n")
		TEXT("v 10.0 0.0 0.0\n")
		TEXT("v 0.0 10.0 0.0\n")
		TEXT("f 1 2 3\n");
	FFileHelper::SaveStringToFile(ObjContent, *ObjPath);

	FString OutError;
	const FGuid AssetId = AssetLib->ImportModelFromFile(ObjPath, OutError);
	TestTrue(TEXT("AssetId must be valid after import"), AssetId.IsValid());
	TestEqual(TEXT("Asset library count should be 1"), AssetLib->GetAssets().Num(), 1);

	const FTerraDyneImportedModelAsset* FoundAsset = AssetLib->FindAsset(AssetId);
	TestNotNull(TEXT("FindAsset must return valid pointer"), FoundAsset);
	if (FoundAsset)
	{
		TestEqual(TEXT("Found asset name should match"), FoundAsset->AssetName, TEXT("LibraryTest"));
		TestTrue(TEXT("Bounding dimensions should be > 0"), FoundAsset->Dimensions.X > 0.0);
	}

	// Remove asset test
	const bool bRemoved = AssetLib->RemoveAsset(AssetId);
	TestTrue(TEXT("RemoveAsset must succeed"), bRemoved);
	TestEqual(TEXT("Asset library count should be 0"), AssetLib->GetAssets().Num(), 0);

	return true;
}

// -----------------------------------------------------------------------------
// Test 7: Placed Actor and Standard Gizmo Manipulation
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDynePlacedModelGizmoTest, "TerraDyne.ModelImport.PlacedActorGizmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDynePlacedModelGizmoTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	UGizmoManipulationSubsystem* GizmoSubsystem = World->GetSubsystem<UGizmoManipulationSubsystem>();
	TestNotNull(TEXT("UGizmoManipulationSubsystem must be valid"), GizmoSubsystem);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATerraDynePlacedModelActor* PlacedActor = World->SpawnActor<ATerraDynePlacedModelActor>(
		ATerraDynePlacedModelActor::StaticClass(),
		FTransform(FVector(100.0f, 200.0f, 50.0f)),
		SpawnParams);

	TestNotNull(TEXT("PlacedActor must spawn"), PlacedActor);
	if (PlacedActor && GizmoSubsystem)
	{
		// Attach gizmo
		GizmoSubsystem->SetTarget(PlacedActor->GetRootComponent());
		TestEqual(TEXT("Gizmo target must be PlacedActor root"), GizmoSubsystem->GetTarget(), PlacedActor->GetRootComponent());

		// Mode switching
		GizmoSubsystem->SetMode(EGizmoMode::Translate);
		TestEqual(TEXT("Gizmo mode should be Translate"), GizmoSubsystem->GetMode(), EGizmoMode::Translate);

		GizmoSubsystem->SetMode(EGizmoMode::Rotate);
		TestEqual(TEXT("Gizmo mode should be Rotate"), GizmoSubsystem->GetMode(), EGizmoMode::Rotate);

		GizmoSubsystem->SetMode(EGizmoMode::Scale);
		TestEqual(TEXT("Gizmo mode should be Scale"), GizmoSubsystem->GetMode(), EGizmoMode::Scale);

		// Visibility toggle
		PlacedActor->SetVisibleInScene(false);
		TestTrue(TEXT("PlacedActor should be hidden"), PlacedActor->IsHidden());
		PlacedActor->SetVisibleInScene(true);
		TestFalse(TEXT("PlacedActor should be visible"), PlacedActor->IsHidden());

		// Scale sync
		PlacedActor->SetActorScale3D(FVector(2.0f, 2.0f, 2.0f));
		PlacedActor->NotifyScaleChanged();
		TestEqual(TEXT("ReplicatedScale3D should match actor scale"), PlacedActor->ReplicatedScale3D, FVector(2.0f, 2.0f, 2.0f));

		GizmoSubsystem->ClearTarget();
		TestNull(TEXT("Gizmo target must be null after ClearTarget"), GizmoSubsystem->GetTarget());

		PlacedActor->Destroy();
	}

	return true;
}

#endif // WITH_EDITOR
