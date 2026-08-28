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
#include "Components/DynamicMeshComponent.h"
#include "CollisionQueryParams.h"
#include "PhysicsEngine/BodySetup.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "ImageUtils.h"

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

// -----------------------------------------------------------------------------
// Test 8: Placed actor really receives geometry AND becomes traceable
//
// Test 7 above spawns a bare actor and only flips gizmo mode enums -- it never calls
// SetModelAsset, which is why two separate runtime defects survived a fully green suite:
//   * ApplyMaterials used ConstructorHelpers::FObjectFinder outside a constructor. That is Fatal
//     (ConstructorHelpers::CheckIfIsInConstructor, UObjectGlobals.cpp), so the first model ever
//     placed crashed the process. Reaching the end of SetModelAsset IS the regression assertion.
//   * The UDynamicMeshComponent never enabled complex collision, and ContainsPhysicsTriMeshData
//     gates entirely on that flag, so placed models had no collision geometry whatsoever and were
//     invisible to ATerraDyneEditController::TraceUnderCursor -- unclickable, unselectable.
//
// So this asserts the actual downstream contract the editor depends on (a world line trace on the
// same channel TraceUnderCursor uses hits the actor), not merely "import did not crash".
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDynePlacedModelCollisionTest, "TerraDyne.ModelImport.PlacedActorCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDynePlacedModelCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>();
	TestNotNull(TEXT("Asset library subsystem must be valid"), AssetLib);
	if (!AssetLib) return false;

	// A closed 200x200x200 box. Deliberately a solid rather than a single quad: a flat, infinitely
	// thin face makes the result depend on backface handling, whereas a ray through a closed solid
	// must cross a face no matter which way the winding runs -- and a solid is what a real imported
	// model looks like anyway. OBJ positions are taken verbatim (ImportOBJ applies no axis or unit
	// conversion), so a straight-down trace through the origin must cross the top and bottom faces.
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("CollisionBox.obj"));
	const FString ObjContent =
		TEXT("v -100.0 -100.0 0.0\n")
		TEXT("v 100.0 -100.0 0.0\n")
		TEXT("v 100.0 100.0 0.0\n")
		TEXT("v -100.0 100.0 0.0\n")
		TEXT("v -100.0 -100.0 200.0\n")
		TEXT("v 100.0 -100.0 200.0\n")
		TEXT("v 100.0 100.0 200.0\n")
		TEXT("v -100.0 100.0 200.0\n")
		TEXT("f 1 3 2\n") TEXT("f 1 4 3\n")
		TEXT("f 5 6 7\n") TEXT("f 5 7 8\n")
		TEXT("f 1 2 6\n") TEXT("f 1 6 5\n")
		TEXT("f 2 3 7\n") TEXT("f 2 7 6\n")
		TEXT("f 3 4 8\n") TEXT("f 3 8 7\n")
		TEXT("f 4 1 5\n") TEXT("f 4 5 8\n");
	FFileHelper::SaveStringToFile(ObjContent, *ObjPath);

	FString OutError;
	const FGuid AssetId = AssetLib->ImportModelFromFile(ObjPath, OutError);
	TestTrue(TEXT("Box import must succeed"), AssetId.IsValid());
	const FTerraDyneImportedModelAsset* Asset = AssetLib->FindAsset(AssetId);
	TestNotNull(TEXT("Imported box must be in the library"), Asset);
	if (!Asset) return false;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATerraDynePlacedModelActor* Placed = World->SpawnActor<ATerraDynePlacedModelActor>(
		ATerraDynePlacedModelActor::StaticClass(), FTransform(FVector::ZeroVector), SpawnParams);
	TestNotNull(TEXT("Placed model actor must spawn"), Placed);
	if (!Placed) return false;

	// Was Fatal before the ApplyMaterials fix -- surviving this line is the regression guard.
	Placed->SetModelAsset(*Asset);
	AddInfo(TEXT("SetModelAsset returned without a fatal error."));

	UDynamicMeshComponent* MeshComp = Placed->DynamicMeshComponent;
	TestNotNull(TEXT("DynamicMeshComponent must exist"), MeshComp);
	if (!MeshComp) return false;

	TestEqual(TEXT("Component mesh must carry the box's 12 triangles"), MeshComp->GetMesh()->TriangleCount(), 12);
	TestTrue(TEXT("Component must report complex physics geometry"), MeshComp->ContainsPhysicsTriMeshData(true));

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TerraDyneModelCollisionTest), /*bInTraceComplex*/ true);
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, FVector(0.0, 0.0, 500.0), FVector(0.0, 0.0, -500.0), ECC_WorldStatic, Params);
	AddInfo(FString::Printf(TEXT("Downward trace hit=%d actor=%s"),
		bHit ? 1 : 0, Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none")));

	if (UBodySetup* BS = MeshComp->GetBodySetup())
	{
		// Cooked triangle geometry is what the trace actually consumes; without the collision fix
		// this is 0 and every trace passes through the model.
		AddInfo(FString::Printf(TEXT("BodySetup TriMeshGeometries=%d bCreatedPhysicsMeshes=%d"),
			BS->TriMeshGeometries.Num(), BS->bCreatedPhysicsMeshes ? 1 : 0));
		TestTrue(TEXT("BodySetup must hold cooked triangle geometry"), BS->TriMeshGeometries.Num() > 0);
	}

	TestTrue(TEXT("A downward trace must hit the placed model -- click-selection depends on it"), bHit);
	TestTrue(TEXT("The trace must hit the placed model actor itself"), Hit.GetActor() == Placed);

	Placed->Destroy();
	return true;
}

// -----------------------------------------------------------------------------
// Test 9: FBX handedness
//
// Unreal is LEFT-handed Z-up. ufbx's two Z-up presets differ only in their `front` axis
// (ufbx_axes_right_handed_z_up = -Y, ufbx_axes_left_handed_z_up = +Y), so targeting the
// right-handed one mirrored every imported FBX through the Y plane and reversed its winding --
// models rendered inside-out. Test 4 above cannot see this: it asserts only TriangleCount >= 1,
// which holds either way, and its fixture is flat in Z so both presets produce identical output.
//
// This fixture declares its source axes explicitly (Y-up, right-handed: the FBX standard) and
// gives one vertex a non-zero source Z, which is the only component the two presets disagree on.
// Source +Z must land on +Y in Unreal. Under the old setting it landed on -Y instead, so the
// bounds assertion below flips sign and fails. Magnitudes are deliberately not asserted -- they
// depend on FBX UnitScaleFactor -- only the sign, which is what encodes handedness.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportFBXHandednessTest, "TerraDyne.ModelImport.FBXHandedness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportFBXHandednessTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	const FString FbxPath = FPaths::Combine(TestDir, TEXT("HandednessTriangle.fbx"));
	const FString FbxContent =
		TEXT("; FBX 7.4.0 project file\n")
		TEXT("FBXHeaderExtension: {\n")
		TEXT("    FBXHeaderVersion: 1003\n")
		TEXT("    FBXVersion: 7400\n")
		TEXT("}\n")
		TEXT("GlobalSettings: {\n")
		TEXT("    Version: 1000\n")
		TEXT("    Properties70: {\n")
		TEXT("        P: \"UpAxis\", \"int\", \"Integer\", \"\",1\n")
		TEXT("        P: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n")
		TEXT("        P: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n")
		TEXT("        P: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n")
		TEXT("        P: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n")
		TEXT("        P: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n")
		TEXT("        P: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n")
		TEXT("    }\n")
		TEXT("}\n")
		TEXT("Definitions: {\n")
		TEXT("    Count: 2\n")
		TEXT("    ObjectType: \"Model\" { Count: 1 }\n")
		TEXT("    ObjectType: \"Geometry\" { Count: 1 }\n")
		TEXT("}\n")
		TEXT("Objects: {\n")
		TEXT("    Geometry: 1000, \"Geometry::Mesh\", \"Mesh\" {\n")
		// v0 (0,0,0), v1 (+X), v2 (+Z in source space) -- v2 is the discriminating vertex.
		TEXT("        Vertices: *9 {\n")
		TEXT("            a: 0,0,0,10,0,0,0,0,10\n")
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
	TestTrue(TEXT("Handedness fixture must import"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(FString::Printf(TEXT("FBX import failed: %s"), *Result.ErrorMessage));
		return false;
	}

	const FBox3d& Bounds = Result.BoundingBox;
	AddInfo(FString::Printf(TEXT("Imported FBX bounds: Min=(%.3f, %.3f, %.3f) Max=(%.3f, %.3f, %.3f)"),
		Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z));

	TestTrue(TEXT("A source +Z vertex must map to +Y in Unreal (left-handed Z-up)"), Bounds.Max.Y > KINDA_SMALL_NUMBER);
	TestTrue(TEXT("No vertex may land on -Y -- that is the mirrored, right-handed result"), Bounds.Min.Y >= -KINDA_SMALL_NUMBER);

	return true;
}

// -----------------------------------------------------------------------------
// Test 10: Library dedupe and explicit asset ids
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelLibraryDedupeTest, "TerraDyne.ModelImport.LibraryDedupe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelLibraryDedupeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>();
	TestNotNull(TEXT("Asset library must exist"), AssetLib);
	if (!AssetLib) return false;

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("DedupeTri.obj"));
	FFileHelper::SaveStringToFile(
		FString(TEXT("v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n")), *ObjPath);

	FString Error;
	const FGuid First = AssetLib->ImportModelFromFile(ObjPath, Error);
	TestTrue(TEXT("First import succeeds"), First.IsValid());
	TestEqual(TEXT("Library holds one entry"), AssetLib->GetAssets().Num(), 1);

	// Importing the same file again must reuse the entry, including via a differently-spelled path.
	const FGuid Second = AssetLib->ImportModelFromFile(ObjPath, Error);
	TestEqual(TEXT("Re-importing the same path returns the same id"), Second, First);
	TestEqual(TEXT("Library still holds one entry"), AssetLib->GetAssets().Num(), 1);

	const FGuid Third = AssetLib->ImportModelFromFile(ObjPath.ToUpper(), Error);
	TestEqual(TEXT("Path comparison is case-insensitive"), Third, First);
	TestEqual(TEXT("Library still holds one entry after a case variant"), AssetLib->GetAssets().Num(), 1);

	// An explicit id is what lets a save game restore its catalog under the ids its placements
	// already reference.
	AssetLib->ClearLibrary();
	const FGuid Forced(0x11111111, 0x22222222, 0x33333333, 0x44444444);
	const FGuid Restored = AssetLib->ImportModelFromFile(ObjPath, Error, Forced);
	TestEqual(TEXT("An explicit asset id is honoured"), Restored, Forced);
	TestNotNull(TEXT("The forced id resolves"), AssetLib->FindAsset(Forced));

	return true;
}

// -----------------------------------------------------------------------------
// Test 11: ResolveOrImport -- the path a replicated placement takes on a client that
// never ran the original import. Previously OnRep_AssetId looked the id up in the local
// catalog and gave up on a miss, so such a client drew an empty actor forever.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelResolveOrImportTest, "TerraDyne.ModelImport.ResolveOrImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelResolveOrImportTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>();
	if (!AssetLib) return false;
	AssetLib->ClearLibrary();

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("RemoteTri.obj"));
	FFileHelper::SaveStringToFile(
		FString(TEXT("v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n")), *ObjPath);

	const FGuid RemoteId(0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD);
	TestNull(TEXT("The catalog starts without the remote id"), AssetLib->FindAsset(RemoteId));

	FString Error;
	const FTerraDyneImportedModelAsset* Resolved = AssetLib->ResolveOrImport(RemoteId, ObjPath, Error);
	TestNotNull(TEXT("ResolveOrImport imports an unseen model on demand"), Resolved);
	if (Resolved)
	{
		TestEqual(TEXT("It is stored under the replicated id, not a fresh one"), Resolved->AssetId, RemoteId);
		TestEqual(TEXT("Geometry actually came through"), Resolved->TriangleCount, 1);
	}

	// A path this machine does not have must fail cleanly with a reason, not crash or half-register.
	FString MissingError;
	const FGuid MissingId(1, 2, 3, 4);
	TestNull(TEXT("A missing source file resolves to nothing"),
		AssetLib->ResolveOrImport(MissingId, FPaths::Combine(TestDir, TEXT("does_not_exist.obj")), MissingError));
	TestTrue(TEXT("...and explains why"), !MissingError.IsEmpty());

	return true;
}

// -----------------------------------------------------------------------------
// Test 12: Save/load round trip through the manager
//
// Imported geometry belongs to no package, so a save can only carry source paths and re-import on
// load. This asserts the whole chain end to end: catalog ids survive, placements come back at the
// transforms they were saved at, and their geometry actually resolves rather than restoring as
// empty actors.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelPersistenceTest, "TerraDyne.ModelImport.Persistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelPersistenceTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull(TEXT("Manager must spawn"), Manager);
	UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>();
	TestNotNull(TEXT("Asset library must exist"), AssetLib);
	if (!Manager || !AssetLib) return false;
	AssetLib->ClearLibrary();

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("PersistTri.obj"));
	FFileHelper::SaveStringToFile(
		FString(TEXT("v 0 0 0\nv 50 0 0\nv 0 50 0\nf 1 2 3\n")), *ObjPath);

	FString Error;
	const FGuid AssetId = AssetLib->ImportModelFromFile(ObjPath, Error);
	TestTrue(TEXT("Import for persistence succeeds"), AssetId.IsValid());
	const FTerraDyneImportedModelAsset* Asset = AssetLib->FindAsset(AssetId);
	if (!Asset) return false;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform TransformA(FRotator(0.0, 45.0, 0.0), FVector(500.0, -250.0, 120.0), FVector(2.0, 2.0, 2.0));
	const FTransform TransformB(FRotator::ZeroRotator, FVector(-800.0, 300.0, 40.0), FVector::OneVector);

	ATerraDynePlacedModelActor* ModelA = World->SpawnActor<ATerraDynePlacedModelActor>(
		ATerraDynePlacedModelActor::StaticClass(), TransformA, SpawnParams);
	ATerraDynePlacedModelActor* ModelB = World->SpawnActor<ATerraDynePlacedModelActor>(
		ATerraDynePlacedModelActor::StaticClass(), TransformB, SpawnParams);
	if (!ModelA || !ModelB) return false;
	ModelA->SetModelAsset(*Asset);
	ModelB->SetModelAsset(*Asset);
	ModelA->CustomLabel = TEXT("First Placement");

	UTerraDyneSaveGame* SaveInst = Manager->BuildSaveGameInstance();
	TestNotNull(TEXT("Save game instance built"), SaveInst);
	if (!SaveInst) return false;

	TestEqual(TEXT("Catalog is saved"), SaveInst->ModelLibrary.Num(), 1);
	TestEqual(TEXT("Both placements are saved"), SaveInst->PlacedModels.Num(), 2);
	TestEqual(TEXT("Saved catalog entry keeps its source path"),
		FPaths::GetCleanFilename(SaveInst->ModelLibrary[0].SourceFilePath), FString(TEXT("PersistTri.obj")));

	Manager->ApplyLoadedSaveGame(SaveInst, TEXT("TestModelPersistence"));

	// Catalog: same ids, or every restored placement would dangle.
	TestEqual(TEXT("Catalog restored to one entry"), AssetLib->GetAssets().Num(), 1);
	TestNotNull(TEXT("Restored catalog keeps the original asset id"), AssetLib->FindAsset(AssetId));

	TArray<AActor*> Restored;
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDynePlacedModelActor::StaticClass(), Restored);
	TestEqual(TEXT("Both placements restored, and the originals were not duplicated"), Restored.Num(), 2);

	bool bFoundA = false;
	for (AActor* Actor : Restored)
	{
		ATerraDynePlacedModelActor* Model = Cast<ATerraDynePlacedModelActor>(Actor);
		if (!Model) continue;

		TestTrue(TEXT("Every restored model resolved real geometry"),
			Model->DynamicMeshComponent && Model->DynamicMeshComponent->GetMesh()->TriangleCount() == 1);
		TestEqual(TEXT("Every restored model kept its asset id"), Model->AssetId, AssetId);

		if (Model->CustomLabel == TEXT("First Placement"))
		{
			bFoundA = true;
			TestTrue(TEXT("Restored location matches what was saved"),
				Model->GetActorLocation().Equals(TransformA.GetLocation(), 0.1));
			TestTrue(TEXT("Restored rotation matches what was saved"),
				Model->GetActorRotation().Equals(TransformA.Rotator(), 0.1f));
			TestTrue(TEXT("Restored scale matches what was saved -- FRepMovement carries no Scale3D"),
				Model->GetActorScale3D().Equals(TransformA.GetScale3D(), 0.01));
		}
	}
	TestTrue(TEXT("The labelled placement came back"), bFoundA);

	return true;
}

// -----------------------------------------------------------------------------
// Test 13: Imported material textures survive garbage collection
//
// FTerraDyneImportedModelAsset::Materials had no UPROPERTY, so the collector never walked into it
// and every imported texture was collected while the catalog still held a TObjectPtr to it. Models
// placed before a GC kept their look (the material instance held its own reference); anything
// placed afterwards came up untextured.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelTextureGCTest, "TerraDyne.ModelImport.MaterialTextureSurvivesGC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelTextureGCTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World) return false;
	UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>();
	if (!AssetLib) return false;
	AssetLib->ClearLibrary();

	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestModelImport"));
	IFileManager::Get().MakeDirectory(*TestDir, true);

	// A real 4x4 PNG for the material to point at. PNGCompressImageArray rather than FImage::Init,
	// which lives in ImageCore and would mean a new module dependency for one test fixture.
	TArray<FColor> Pixels;
	Pixels.Init(FColor(200, 80, 40, 255), 16);
	TArray64<uint8> PngData;
	FImageUtils::PNGCompressImageArray(4, 4, Pixels, PngData);
	if (PngData.Num() == 0)
	{
		AddError(TEXT("Could not encode the test PNG."));
		return false;
	}
	const FString PngPath = FPaths::Combine(TestDir, TEXT("GCTexture.png"));
	FFileHelper::SaveArrayToFile(PngData, *PngPath);


	FFileHelper::SaveStringToFile(
		FString(TEXT("newmtl GCMat\nKd 0.8 0.3 0.15\nmap_Kd GCTexture.png\n")),
		*FPaths::Combine(TestDir, TEXT("GCModel.mtl")));
	const FString ObjPath = FPaths::Combine(TestDir, TEXT("GCModel.obj"));
	FFileHelper::SaveStringToFile(
		FString(TEXT("mtllib GCModel.mtl\nv 0 0 0\nv 10 0 0\nv 0 10 0\nusemtl GCMat\nf 1 2 3\n")), *ObjPath);

	FString Error;
	const FGuid AssetId = AssetLib->ImportModelFromFile(ObjPath, Error);
	TestTrue(TEXT("Textured model imports"), AssetId.IsValid());

	const FTerraDyneImportedModelAsset* Asset = AssetLib->FindAsset(AssetId);
	TestNotNull(TEXT("Textured model is in the catalog"), Asset);
	if (!Asset) return false;
	// By name, not by index: ImportOBJ always seeds slot 0 with a synthesised "DefaultMaterial", so
	// the one parsed from the .mtl lands at 1.
	auto FindTexturedSlot = [](const FTerraDyneImportedModelAsset& InAsset) -> const FTerraDyneImportedMaterial*
	{
		return InAsset.Materials.FindByPredicate(
			[](const FTerraDyneImportedMaterial& Mat) { return Mat.MaterialName == TEXT("GCMat"); });
	};

	const FTerraDyneImportedMaterial* SlotBefore = FindTexturedSlot(*Asset);
	TestNotNull(TEXT("The .mtl's material slot was parsed"), SlotBefore);
	if (!SlotBefore) return false;
	TestNotNull(TEXT("The diffuse texture loaded"), SlotBefore->DiffuseTexture.Get());
	if (!SlotBefore->DiffuseTexture.Get()) return false;

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);

	// Re-fetch through the catalog rather than reusing the old pointer: if the texture had been
	// collected, the reflected TObjectPtr is what would have been nulled out.
	const FTerraDyneImportedModelAsset* AfterGC = AssetLib->FindAsset(AssetId);
	TestNotNull(TEXT("The catalog entry survives GC"), AfterGC);
	if (!AfterGC) return false;

	const FTerraDyneImportedMaterial* SlotAfter = FindTexturedSlot(*AfterGC);
	TestNotNull(TEXT("The material slot survives GC"), SlotAfter);
	if (SlotAfter)
	{
		TestNotNull(TEXT("The diffuse texture survives GC -- it is reachable through a UPROPERTY"),
			SlotAfter->DiffuseTexture.Get());
	}

	return true;
}

// -----------------------------------------------------------------------------
// Test 14: Smart Texture Path Resolution
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneTextureResolutionTest, "TerraDyne.ModelImport.TextureResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneTextureResolutionTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestTextureResolution"));
	const FString TexturesSubDir = FPaths::Combine(TestDir, TEXT("textures"));
	IFileManager::Get().MakeDirectory(*TexturesSubDir, true);

	const FString ModelPath = FPaths::Combine(TestDir, TEXT("House.fbx"));
	const FString RealTexturePath = FPaths::Combine(TexturesSubDir, TEXT("Roof_BaseColor.png"));

	// Create a 1x1 PNG file
	TArray<uint8> PngBytes = {
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
		0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
		0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
		0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
		0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
	};
	FFileHelper::SaveArrayToFile(PngBytes, *RealTexturePath);

	// Case A: Relative path "textures/Roof_BaseColor.png"
	FString ResolvedA = FTerraDyneModelImporter::ResolveTexturePath(ModelPath, TEXT("textures/Roof_BaseColor.png"));
	TestEqual(TEXT("Relative path in subfolder resolves"), ResolvedA, RealTexturePath);

	// Case B: Foreign artist absolute path "D:\\Author\\3D\\Textures\\Roof_BaseColor.png"
	FString ResolvedB = FTerraDyneModelImporter::ResolveTexturePath(ModelPath, TEXT("D:\\Author\\3D\\Textures\\Roof_BaseColor.png"));
	TestEqual(TEXT("Foreign absolute path resolves via subfolder search"), ResolvedB, RealTexturePath);

	// Case C: Extension fallback (FBX specifies .tga, but disk has .png)
	FString ResolvedC = FTerraDyneModelImporter::ResolveTexturePath(ModelPath, TEXT("Roof_BaseColor.tga"));
	TestEqual(TEXT("Extension fallback resolves .tga to .png"), ResolvedC, RealTexturePath);

	return true;
}

// -----------------------------------------------------------------------------
// Test 15: FBX Texture Import
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneModelImportFBXTextureTest, "TerraDyne.ModelImport.FBXTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneModelImportFBXTextureTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestFBXTexture"));
	const FString TexturesSubDir = FPaths::Combine(TestDir, TEXT("textures"));
	IFileManager::Get().MakeDirectory(*TexturesSubDir, true);

	const FString FbxPath = FPaths::Combine(TestDir, TEXT("CubeTextured.fbx"));
	const FString TexPath = FPaths::Combine(TexturesSubDir, TEXT("CubeDiffuse.png"));

	TArray<uint8> PngBytes = {
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
		0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
		0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
		0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
		0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
	};
	FFileHelper::SaveArrayToFile(PngBytes, *TexPath);

	const FString FbxContent =
		TEXT("; FBX 7.4.0 project file\n")
		TEXT("FBXHeaderExtension: {\n")
		TEXT("    FBXHeaderVersion: 1003\n")
		TEXT("    FBXVersion: 7400\n")
		TEXT("}\n")
		TEXT("Definitions: {\n")
		TEXT("    Count: 4\n")
		TEXT("    ObjectType: \"Model\" { Count: 1 }\n")
		TEXT("    ObjectType: \"Geometry\" { Count: 1 }\n")
		TEXT("    ObjectType: \"Material\" { Count: 1 }\n")
		TEXT("    ObjectType: \"Texture\" { Count: 1 }\n")
		TEXT("}\n")
		TEXT("Objects: {\n")
		TEXT("    Geometry: 1000, \"Geometry::Mesh\", \"Mesh\" {\n")
		TEXT("        Vertices: *9 { a: 0,0,0,10,0,0,0,10,0 }\n")
		TEXT("        PolygonVertexIndex: *3 { a: 0,1,-3 }\n")
		TEXT("    }\n")
		TEXT("    Model: 2000, \"Model::TestMesh\", \"Mesh\" { Version: 232 }\n")
		TEXT("    Material: 3000, \"Material::CubeMat\", \"\" {\n")
		TEXT("        Properties70: {\n")
		TEXT("            P: \"DiffuseColor\", \"ColorRGB\", \"Color\", \"\",0.8,0.8,0.8\n")
		TEXT("        }\n")
		TEXT("    }\n")
		TEXT("    Texture: 4000, \"Texture::CubeTex\", \"\" {\n")
		TEXT("        FileName: \"textures/CubeDiffuse.png\"\n")
		TEXT("        RelativeFilename: \"textures/CubeDiffuse.png\"\n")
		TEXT("    }\n")
		TEXT("}\n")
		TEXT("Connections: {\n")
		TEXT("    C: \"OO\",1000,2000\n")
		TEXT("    C: \"OO\",2000,0\n")
		TEXT("    C: \"OO\",3000,2000\n")
		TEXT("    C: \"OP\",4000,3000, \"DiffuseColor\"\n")
		TEXT("}\n");

	FFileHelper::SaveStringToFile(FbxContent, *FbxPath);

	FTerraDyneImportModelResult Result = FTerraDyneModelImporter::ImportModelFromFile(FbxPath);
	TestTrue(TEXT("Textured FBX must import successfully"), Result.bSuccess);
	TestTrue(TEXT("Materials array must have at least 1 material"), Result.Materials.Num() >= 1);
	if (Result.Materials.Num() >= 1)
	{
		TestNotNull(TEXT("DiffuseTexture must be loaded on imported FBX material"), Result.Materials[0].DiffuseTexture.Get());
	}

	return true;
}

#endif // WITH_EDITOR
