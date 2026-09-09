// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/GRTModelImporter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/Texture2D.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTransforms.h"
#include "GRTModule.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#pragma warning(disable : 4127)
#pragma warning(disable : 4702)
#pragma warning(disable : 4701)
#endif

#include "ThirdParty/ufbx/ufbx.h"

#define CGLTF_IMPLEMENTATION
#include "ThirdParty/cgltf/cgltf.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace UE::Geometry;

bool FGRTModelImporter::IsSupportedExtension(const FString& Extension)
{
	const FString Lower = Extension.ToLower();
	return Lower == TEXT("obj") || Lower == TEXT("stl") || Lower == TEXT("ply") ||
	       Lower == TEXT("fbx") || Lower == TEXT("gltf") || Lower == TEXT("glb");
}

FString FGRTModelImporter::GetSupportedFileFilter()
{
	return TEXT("3D Models (*.obj;*.stl;*.ply;*.fbx;*.gltf;*.glb)|*.obj;*.stl;*.ply;*.fbx;*.gltf;*.glb;*.OBJ;*.STL;*.PLY;*.FBX;*.GLTF;*.GLB|Wavefront OBJ (*.obj)|*.obj|Autodesk FBX (*.fbx)|*.fbx|glTF 2.0 (*.gltf;*.glb)|*.gltf;*.glb|Stereolithography STL (*.stl)|*.stl|Polygon File Format PLY (*.ply)|*.ply|All Files (*.*)|*.*");
}

FGRTImportModelResult FGRTModelImporter::ImportModelFromFile(const FString& FilePath)
{
	FGRTImportModelResult Result;
	Result.SourceFilePath = FilePath;
	Result.ModelName = FPaths::GetBaseFilename(FilePath);

	const FString Ext = FPaths::GetExtension(FilePath).ToLower();
	if (Ext == TEXT("obj"))
	{
		ImportOBJ(FilePath, Result);
	}
	else if (Ext == TEXT("stl"))
	{
		ImportSTL(FilePath, Result);
	}
	else if (Ext == TEXT("ply"))
	{
		ImportPLY(FilePath, Result);
	}
	else if (Ext == TEXT("fbx"))
	{
		ImportFBX(FilePath, Result);
	}
	else if (Ext == TEXT("gltf") || Ext == TEXT("glb"))
	{
		ImportGLTF(FilePath, Result);
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Unsupported 3D model format extension: .%s"), *Ext);
	}

	if (Result.bSuccess)
	{
		Result.VertexCount = Result.Mesh.VertexCount();
		Result.TriangleCount = Result.Mesh.TriangleCount();
		const FAxisAlignedBox3d Bounds = Result.Mesh.GetBounds(true);
		Result.BoundingBox = FBox3d(Bounds.Min, Bounds.Max);
	}

	return Result;
}

UTexture2D* FGRTModelImporter::LoadTextureFromFile(const FString& FilePath)
{
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
	{
		return nullptr;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath) || FileData.Num() == 0)
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return nullptr;
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	if (Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	UTexture2D* NewTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!NewTexture)
	{
		return nullptr;
	}

	void* TextureData = NewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	NewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	NewTexture->UpdateResource();

	return NewTexture;
}

// -----------------------------------------------------------------------------
// Wavefront OBJ Importer
// -----------------------------------------------------------------------------
namespace
{
	struct FOBJIndex
	{
		int32 V = -1;
		int32 VT = -1;
		int32 VN = -1;
	};

	static FOBJIndex ParseOBJIndex(const FString& Token, int32 NumV, int32 NumVT, int32 NumVN)
	{
		FOBJIndex Idx;
		TArray<FString> Parts;
		Token.ParseIntoArray(Parts, TEXT("/"), false);

		if (Parts.Num() >= 1 && !Parts[0].IsEmpty())
		{
			const int32 Val = FCString::Atoi(*Parts[0]);
			Idx.V = (Val < 0) ? (NumV + Val) : (Val - 1);
		}
		if (Parts.Num() >= 2 && !Parts[1].IsEmpty())
		{
			const int32 Val = FCString::Atoi(*Parts[1]);
			Idx.VT = (Val < 0) ? (NumVT + Val) : (Val - 1);
		}
		if (Parts.Num() >= 3 && !Parts[2].IsEmpty())
		{
			const int32 Val = FCString::Atoi(*Parts[2]);
			Idx.VN = (Val < 0) ? (NumVN + Val) : (Val - 1);
		}
		return Idx;
	}
}

bool FGRTModelImporter::ImportOBJ(const FString& FilePath, FGRTImportModelResult& OutResult)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath))
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to open OBJ file: %s"), *FilePath);
		return false;
	}

	TArray<FVector3d> Positions;
	TArray<FVector2f> UVs;
	TArray<FVector3f> Normals;
	TMap<FString, int32> MaterialNameToIndex;

	const FString Directory = FPaths::GetPath(FilePath);
	FString CurrentMaterialName;
	int32 CurrentMaterialIndex = 0;

	// Default material
	FGRTImportedMaterial DefaultMat;
	DefaultMat.MaterialName = TEXT("DefaultMaterial");
	DefaultMat.DiffuseColor = FLinearColor::White;
	OutResult.Materials.Add(DefaultMat);
	MaterialNameToIndex.Add(DefaultMat.MaterialName, 0);

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);

	// First pass for .mtl library loading
	for (const FString& Line : Lines)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("mtllib ")))
		{
			FString MtlFileName = Trimmed.Mid(7).TrimStartAndEnd();
			FString MtlPath = FPaths::Combine(Directory, MtlFileName);
			FString MtlContent;
			if (FFileHelper::LoadFileToString(MtlContent, *MtlPath))
			{
				TArray<FString> MtlLines;
				MtlContent.ParseIntoArrayLines(MtlLines);
				FGRTImportedMaterial CurrentMtl;

				for (const FString& MLine : MtlLines)
				{
					FString MTrimmed = MLine.TrimStartAndEnd();
					if (MTrimmed.StartsWith(TEXT("newmtl ")))
					{
						if (!CurrentMtl.MaterialName.IsEmpty())
						{
							int32 MatIdx = OutResult.Materials.Add(CurrentMtl);
							MaterialNameToIndex.Add(CurrentMtl.MaterialName, MatIdx);
						}
						CurrentMtl = FGRTImportedMaterial();
						CurrentMtl.MaterialName = MTrimmed.Mid(7).TrimStartAndEnd();
					}
					else if (MTrimmed.StartsWith(TEXT("Kd ")))
					{
						TArray<FString> Tokens;
						MTrimmed.Mid(3).TrimStartAndEnd().ParseIntoArrayWS(Tokens);
						if (Tokens.Num() >= 3)
						{
							CurrentMtl.DiffuseColor = FLinearColor(
								FCString::Atof(*Tokens[0]),
								FCString::Atof(*Tokens[1]),
								FCString::Atof(*Tokens[2])
							);
						}
					}
					else if (MTrimmed.StartsWith(TEXT("map_Kd ")))
					{
						FString TexRelPath = MTrimmed.Mid(7).TrimStartAndEnd();
						FString ResolvedTexPath = ResolveTexturePath(FilePath, TexRelPath);
						if (!ResolvedTexPath.IsEmpty())
						{
							CurrentMtl.DiffuseTexturePath = ResolvedTexPath;
							CurrentMtl.DiffuseTexture = LoadTextureFromFile(ResolvedTexPath);
						}
					}
				}

				if (!CurrentMtl.MaterialName.IsEmpty())
				{
					int32 MatIdx = OutResult.Materials.Add(CurrentMtl);
					MaterialNameToIndex.Add(CurrentMtl.MaterialName, MatIdx);
				}
			}
		}
	}

	FDynamicMesh3& Mesh = OutResult.Mesh;
	Mesh.Clear();
	Mesh.EnableAttributes();
	Mesh.Attributes()->EnableMaterialID();
	FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
	FDynamicMeshMaterialAttribute* MaterialAttr = Mesh.Attributes()->GetMaterialID();

	TArray<int32> PosToVertexID;
	TArray<int32> UVToUVElementID;
	TArray<int32> NormalToNormElementID;

	// Second pass: Read geometry and build dynamic mesh
	for (const FString& Line : Lines)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")))
		{
			continue;
		}

		TArray<FString> Tokens;
		Trimmed.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() == 0) continue;

		const FString& Tag = Tokens[0];

		if (Tag == TEXT("v") && Tokens.Num() >= 4)
		{
			// OBJ: X Right, Y Up, Z Forward (or -Z) -> Convert to Unreal: X Forward, Y Right, Z Up
			// Common OBJ standard: (X, Y, Z) -> Unreal (Z, X, Y) or direct (X, Y, Z)
			const double X = FCString::Atod(*Tokens[1]);
			const double Y = FCString::Atod(*Tokens[2]);
			const double Z = FCString::Atod(*Tokens[3]);
			const FVector3d Pos(X, Y, Z);
			Positions.Add(Pos);
			const int32 VID = Mesh.AppendVertex(Pos);
			PosToVertexID.Add(VID);
		}
		else if (Tag == TEXT("vt") && Tokens.Num() >= 3)
		{
			const float U = FCString::Atof(*Tokens[1]);
			const float V = 1.0f - FCString::Atof(*Tokens[2]); // Flip V for Unreal
			const FVector2f UV(U, V);
			UVs.Add(UV);
			if (UVOverlay)
			{
				int32 EID = UVOverlay->AppendElement(UV);
				UVToUVElementID.Add(EID);
			}
		}
		else if (Tag == TEXT("vn") && Tokens.Num() >= 4)
		{
			const float NX = FCString::Atof(*Tokens[1]);
			const float NY = FCString::Atof(*Tokens[2]);
			const float NZ = FCString::Atof(*Tokens[3]);
			const FVector3f N = FVector3f(NX, NY, NZ).GetSafeNormal();
			Normals.Add(N);
			if (NormalOverlay)
			{
				int32 EID = NormalOverlay->AppendElement(N);
				NormalToNormElementID.Add(EID);
			}
		}
		else if (Tag == TEXT("usemtl") && Tokens.Num() >= 2)
		{
			CurrentMaterialName = Tokens[1];
			if (const int32* Found = MaterialNameToIndex.Find(CurrentMaterialName))
			{
				CurrentMaterialIndex = *Found;
			}
			else
			{
				CurrentMaterialIndex = 0;
			}
		}
		else if (Tag == TEXT("f") && Tokens.Num() >= 4)
		{
			TArray<FOBJIndex> FaceIndices;
			for (int32 i = 1; i < Tokens.Num(); ++i)
			{
				FaceIndices.Add(ParseOBJIndex(Tokens[i], Positions.Num(), UVs.Num(), Normals.Num()));
			}

			// Triangulate face polygon as fan from vertex 0: (0, i, i+1)
			for (int32 i = 1; i < FaceIndices.Num() - 1; ++i)
			{
				const FOBJIndex& I0 = FaceIndices[0];
				const FOBJIndex& I1 = FaceIndices[i];
				const FOBJIndex& I2 = FaceIndices[i + 1];

				if (I0.V < 0 || I0.V >= PosToVertexID.Num() ||
					I1.V < 0 || I1.V >= PosToVertexID.Num() ||
					I2.V < 0 || I2.V >= PosToVertexID.Num())
				{
					continue;
				}

				const int32 V0 = PosToVertexID[I0.V];
				const int32 V1 = PosToVertexID[I1.V];
				const int32 V2 = PosToVertexID[I2.V];

				const int32 TID = Mesh.AppendTriangle(V0, V1, V2);
				if (TID < 0) continue;

				if (MaterialAttr)
				{
					MaterialAttr->SetValue(TID, CurrentMaterialIndex);
				}

				if (UVOverlay && I0.VT >= 0 && I1.VT >= 0 && I2.VT >= 0 &&
					I0.VT < UVToUVElementID.Num() && I1.VT < UVToUVElementID.Num() && I2.VT < UVToUVElementID.Num())
				{
					UVOverlay->SetTriangle(TID, FIndex3i(UVToUVElementID[I0.VT], UVToUVElementID[I1.VT], UVToUVElementID[I2.VT]));
				}

				if (NormalOverlay && I0.VN >= 0 && I1.VN >= 0 && I2.VN >= 0 &&
					I0.VN < NormalToNormElementID.Num() && I1.VN < NormalToNormElementID.Num() && I2.VN < NormalToNormElementID.Num())
				{
					NormalOverlay->SetTriangle(TID, FIndex3i(NormalToNormElementID[I0.VN], NormalToNormElementID[I1.VN], NormalToNormElementID[I2.VN]));
				}
			}
		}
	}

	if (Mesh.TriangleCount() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("OBJ file contained no valid triangles.");
		return false;
	}

	// Auto-compute mesh normals if none were provided in the file
	if (Normals.Num() == 0 && NormalOverlay)
	{
		FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
	}

	OutResult.bSuccess = true;
	return true;
}

// -----------------------------------------------------------------------------
// Stereolithography STL Importer
// -----------------------------------------------------------------------------
bool FGRTModelImporter::ImportSTL(const FString& FilePath, FGRTImportModelResult& OutResult)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath) || FileData.Num() < 84)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to read STL file: %s"), *FilePath);
		return false;
	}

	FDynamicMesh3& Mesh = OutResult.Mesh;
	Mesh.Clear();
	Mesh.EnableAttributes();
	FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();

	// Check if Binary STL: header is 80 bytes, next 4 bytes uint32 triangle count, total size = 84 + count * 50
	const uint32 NumTriangles = *reinterpret_cast<const uint32*>(FileData.GetData() + 80);
	const int64 ExpectedBinarySize = 84 + static_cast<int64>(NumTriangles) * 50;

	if (FileData.Num() == ExpectedBinarySize && NumTriangles > 0)
	{
		// Binary STL
		const uint8* Ptr = FileData.GetData() + 84;
		for (uint32 TriIdx = 0; TriIdx < NumTriangles; ++TriIdx)
		{
			const float* FloatPtr = reinterpret_cast<const float*>(Ptr);
			const FVector3f Norm(FloatPtr[0], FloatPtr[1], FloatPtr[2]);
			const FVector3d V0(FloatPtr[3], FloatPtr[4], FloatPtr[5]);
			const FVector3d V1(FloatPtr[6], FloatPtr[7], FloatPtr[8]);
			const FVector3d V2(FloatPtr[9], FloatPtr[10], FloatPtr[11]);
			Ptr += 50; // 48 bytes floats + 2 bytes attribute

			const int32 Vid0 = Mesh.AppendVertex(V0);
			const int32 Vid1 = Mesh.AppendVertex(V1);
			const int32 Vid2 = Mesh.AppendVertex(V2);
			const int32 TID = Mesh.AppendTriangle(Vid0, Vid1, Vid2);

			if (TID >= 0 && NormalOverlay)
			{
				const int32 N0 = NormalOverlay->AppendElement(Norm);
				const int32 N1 = NormalOverlay->AppendElement(Norm);
				const int32 N2 = NormalOverlay->AppendElement(Norm);
				NormalOverlay->SetTriangle(TID, FIndex3i(N0, N1, N2));
			}
		}
	}
	else
	{
		// ASCII STL
		FString Content;
		FFileHelper::BufferToString(Content, FileData.GetData(), FileData.Num());
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);

		FVector3f CurrentNormal = FVector3f::UpVector;
		TArray<FVector3d> TriangleVertices;

		for (const FString& Line : Lines)
		{
			FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.StartsWith(TEXT("facet normal ")))
			{
				TArray<FString> Tokens;
				Trimmed.Mid(13).TrimStartAndEnd().ParseIntoArrayWS(Tokens);
				if (Tokens.Num() >= 3)
				{
					CurrentNormal = FVector3f(FCString::Atof(*Tokens[0]), FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2])).GetSafeNormal();
				}
				TriangleVertices.Reset();
			}
			else if (Trimmed.StartsWith(TEXT("vertex ")))
			{
				TArray<FString> Tokens;
				Trimmed.Mid(7).TrimStartAndEnd().ParseIntoArrayWS(Tokens);
				if (Tokens.Num() >= 3)
				{
					TriangleVertices.Add(FVector3d(FCString::Atod(*Tokens[0]), FCString::Atod(*Tokens[1]), FCString::Atod(*Tokens[2])));
				}
			}
			else if (Trimmed.StartsWith(TEXT("endfacet")) && TriangleVertices.Num() >= 3)
			{
				const int32 Vid0 = Mesh.AppendVertex(TriangleVertices[0]);
				const int32 Vid1 = Mesh.AppendVertex(TriangleVertices[1]);
				const int32 Vid2 = Mesh.AppendVertex(TriangleVertices[2]);
				const int32 TID = Mesh.AppendTriangle(Vid0, Vid1, Vid2);

				if (TID >= 0 && NormalOverlay)
				{
					const int32 N0 = NormalOverlay->AppendElement(CurrentNormal);
					const int32 N1 = NormalOverlay->AppendElement(CurrentNormal);
					const int32 N2 = NormalOverlay->AppendElement(CurrentNormal);
					NormalOverlay->SetTriangle(TID, FIndex3i(N0, N1, N2));
				}
				TriangleVertices.Reset();
			}
		}
	}

	if (Mesh.TriangleCount() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("STL file contained no valid triangles.");
		return false;
	}

	OutResult.bSuccess = true;
	return true;
}

// -----------------------------------------------------------------------------
// Polygon File Format PLY Importer
// -----------------------------------------------------------------------------
bool FGRTModelImporter::ImportPLY(const FString& FilePath, FGRTImportModelResult& OutResult)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath))
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to open PLY file: %s"), *FilePath);
		return false;
	}

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);
	if (Lines.Num() == 0 || !Lines[0].TrimStartAndEnd().Equals(TEXT("ply"), ESearchCase::IgnoreCase))
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("Invalid PLY header magic.");
		return false;
	}

	int32 VertexCount = 0;
	int32 FaceCount = 0;
	int32 HeaderEndLine = -1;

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		FString Trimmed = Lines[i].TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("element vertex ")))
		{
			VertexCount = FCString::Atoi(*Trimmed.Mid(15).TrimStartAndEnd());
		}
		else if (Trimmed.StartsWith(TEXT("element face ")))
		{
			FaceCount = FCString::Atoi(*Trimmed.Mid(13).TrimStartAndEnd());
		}
		else if (Trimmed.Equals(TEXT("end_header"), ESearchCase::IgnoreCase))
		{
			HeaderEndLine = i;
			break;
		}
	}

	if (HeaderEndLine < 0 || VertexCount <= 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("Failed to parse PLY header elements.");
		return false;
	}

	FDynamicMesh3& Mesh = OutResult.Mesh;
	Mesh.Clear();
	Mesh.EnableAttributes();
	FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();

	TArray<int32> PosToVertexID;
	PosToVertexID.Reserve(VertexCount);

	int32 CurrentLine = HeaderEndLine + 1;
	// Read Vertices
	for (int32 v = 0; v < VertexCount && CurrentLine < Lines.Num(); ++v, ++CurrentLine)
	{
		TArray<FString> Tokens;
		Lines[CurrentLine].TrimStartAndEnd().ParseIntoArrayWS(Tokens);
		if (Tokens.Num() >= 3)
		{
			const FVector3d Pos(FCString::Atod(*Tokens[0]), FCString::Atod(*Tokens[1]), FCString::Atod(*Tokens[2]));
			const int32 VID = Mesh.AppendVertex(Pos);
			PosToVertexID.Add(VID);
		}
	}

	// Read Faces
	for (int32 f = 0; f < FaceCount && CurrentLine < Lines.Num(); ++f, ++CurrentLine)
	{
		TArray<FString> Tokens;
		Lines[CurrentLine].TrimStartAndEnd().ParseIntoArrayWS(Tokens);
		if (Tokens.Num() >= 4)
		{
			const int32 N = FCString::Atoi(*Tokens[0]);
			if (N >= 3 && Tokens.Num() >= N + 1)
			{
				TArray<int32> FaceV;
				for (int32 k = 0; k < N; ++k)
				{
					FaceV.Add(FCString::Atoi(*Tokens[k + 1]));
				}

				for (int32 k = 1; k < N - 1; ++k)
				{
					if (FaceV[0] < PosToVertexID.Num() && FaceV[k] < PosToVertexID.Num() && FaceV[k + 1] < PosToVertexID.Num())
					{
						Mesh.AppendTriangle(PosToVertexID[FaceV[0]], PosToVertexID[FaceV[k]], PosToVertexID[FaceV[k + 1]]);
					}
				}
			}
		}
	}

	if (Mesh.TriangleCount() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("PLY file contained no valid triangles.");
		return false;
	}

	if (NormalOverlay)
	{
		FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
	}

	OutResult.bSuccess = true;
	return true;
}

UTexture2D* FGRTModelImporter::LoadTextureFromMemory(const uint8* BufferData, int64 BufferSize, const FString& FormatHint)
{
	if (!BufferData || BufferSize <= 0)
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(BufferData, BufferSize);
	if (ImageFormat == EImageFormat::Invalid)
	{
		const FString HintLower = FormatHint.ToLower();
		if (HintLower.Contains(TEXT("png"))) ImageFormat = EImageFormat::PNG;
		else if (HintLower.Contains(TEXT("jpg")) || HintLower.Contains(TEXT("jpeg"))) ImageFormat = EImageFormat::JPEG;
		else if (HintLower.Contains(TEXT("bmp"))) ImageFormat = EImageFormat::BMP;
		else return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(BufferData, BufferSize))
	{
		return nullptr;
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	if (Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	UTexture2D* NewTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!NewTexture)
	{
		return nullptr;
	}

	void* TextureData = NewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	NewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	NewTexture->UpdateResource();

	return NewTexture;
}

FString FGRTModelImporter::ResolveTexturePath(const FString& ModelFilePath, const FString& DeclaredTexturePath)
{
	if (DeclaredTexturePath.IsEmpty())
	{
		return FString();
	}

	const FString ModelDir = FPaths::GetPath(ModelFilePath);
	const FString CleanDeclared = DeclaredTexturePath.Replace(TEXT("\\"), TEXT("/"));

	// 1. Direct path check (if absolute or relative path exists directly)
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*CleanDeclared))
	{
		return CleanDeclared;
	}

	// 2. Relative to ModelDir
	const FString RelativePath = FPaths::Combine(ModelDir, CleanDeclared);
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*RelativePath))
	{
		return RelativePath;
	}

	// 3. Basename directly in ModelDir
	const FString CleanFileName = FPaths::GetCleanFilename(CleanDeclared);
	const FString InModelDir = FPaths::Combine(ModelDir, CleanFileName);
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*InModelDir))
	{
		return InModelDir;
	}

	// 4. Look in common texture subdirectories inside ModelDir
	static const TArray<FString> SubDirs = {
		TEXT("textures"), TEXT("Textures"), TEXT("texture"), TEXT("Texture"),
		TEXT("images"), TEXT("Images"), TEXT("image"), TEXT("Image"),
		TEXT("maps"), TEXT("Maps"), TEXT("materials"), TEXT("Materials"),
		TEXT("assets"), TEXT("Assets"), TEXT("sourceimages"), TEXT("SourceImages")
	};

	for (const FString& SubDir : SubDirs)
	{
		const FString SubDirPath = FPaths::Combine(ModelDir, SubDir, CleanFileName);
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*SubDirPath))
		{
			return SubDirPath;
		}
	}

	// 5. Look in parent directory and parent's subdirectories
	const FString ParentDir = FPaths::GetPath(ModelDir);
	if (!ParentDir.IsEmpty() && ParentDir != ModelDir)
	{
		const FString InParentDir = FPaths::Combine(ParentDir, CleanFileName);
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*InParentDir))
		{
			return InParentDir;
		}

		for (const FString& SubDir : SubDirs)
		{
			const FString SubDirPath = FPaths::Combine(ParentDir, SubDir, CleanFileName);
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*SubDirPath))
			{
				return SubDirPath;
			}
		}
	}

	// 6. Format fallback: if declared path has .tga, .tif, .tiff, .psd, .dds, check for .png, .jpg, .jpeg, .bmp
	const FString BaseNameWithoutExt = FPaths::GetBaseFilename(CleanDeclared);
	static const TArray<FString> Extensions = {
		TEXT("png"), TEXT("PNG"), TEXT("jpg"), TEXT("JPG"), TEXT("jpeg"), TEXT("JPEG"),
		TEXT("bmp"), TEXT("BMP"), TEXT("tga"), TEXT("TGA"), TEXT("tif"), TEXT("tiff")
	};

	for (const FString& Ext : Extensions)
	{
		const FString CandidateName = BaseNameWithoutExt + TEXT(".") + Ext;
		const FString PathInModelDir = FPaths::Combine(ModelDir, CandidateName);
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*PathInModelDir))
		{
			return PathInModelDir;
		}
		for (const FString& SubDir : SubDirs)
		{
			const FString SubDirPath = FPaths::Combine(ModelDir, SubDir, CandidateName);
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*SubDirPath))
			{
				return SubDirPath;
			}
		}
		if (!ParentDir.IsEmpty() && ParentDir != ModelDir)
		{
			const FString PathInParent = FPaths::Combine(ParentDir, CandidateName);
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*PathInParent))
			{
				return PathInParent;
			}
		}
	}

	return FString();
}

bool FGRTModelImporter::ImportFBX(const FString& FilePath, FGRTImportModelResult& OutResult)
{
	ufbx_load_opts opts = { 0 };
	// Unreal is LEFT-handed Z-up. Targeting right-handed Z-up handed back mirrored geometry with
	// reversed winding, so every FBX rendered inside-out. ufbx flips face winding itself as part of
	// the handedness conversion (handedness_conversion_retain_winding defaults to false), so this
	// one change fixes both the mirroring and the winding.
	opts.target_axes = ufbx_axes_left_handed_z_up;
	opts.target_unit_meters = 1.0f;
	opts.generate_missing_normals = true;

	ufbx_error error;
	ufbx_scene* scene = ufbx_load_file(TCHAR_TO_UTF8(*FilePath), &opts, &error);
	if (!scene)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to load FBX file: %s (%s)"), *FilePath, UTF8_TO_TCHAR(error.description.data));
		return false;
	}

	FDynamicMesh3& Mesh = OutResult.Mesh;
	Mesh.Clear();
	Mesh.EnableAttributes();
	Mesh.Attributes()->EnableMaterialID();
	FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
	FDynamicMeshMaterialAttribute* MaterialAttr = Mesh.Attributes()->GetMaterialID();

	// Read materials
	TMap<const ufbx_material*, int32> MaterialToIndex;
	for (size_t m = 0; m < scene->materials.count; ++m)
	{
		ufbx_material* FbxMat = scene->materials.data[m];
		FGRTImportedMaterial ImportedMat;
		ImportedMat.MaterialName = UTF8_TO_TCHAR(FbxMat->name.data);
		if (ImportedMat.MaterialName.IsEmpty())
		{
			ImportedMat.MaterialName = FString::Printf(TEXT("Material_%d"), (int32)m);
		}

		if (FbxMat->pbr.base_color.has_value)
		{
			ImportedMat.DiffuseColor = FLinearColor(
				FbxMat->pbr.base_color.value_vec4.x,
				FbxMat->pbr.base_color.value_vec4.y,
				FbxMat->pbr.base_color.value_vec4.z,
				FbxMat->pbr.base_color.value_vec4.w
			);
		}
		else if (FbxMat->fbx.diffuse_color.has_value)
		{
			ImportedMat.DiffuseColor = FLinearColor(
				FbxMat->fbx.diffuse_color.value_vec4.x,
				FbxMat->fbx.diffuse_color.value_vec4.y,
				FbxMat->fbx.diffuse_color.value_vec4.z,
				1.0f
			);
		}

		// Inspect all possible FBX texture slots
		ufbx_texture* Tex = nullptr;
		if (FbxMat->pbr.base_color.texture) Tex = FbxMat->pbr.base_color.texture;
		else if (FbxMat->fbx.diffuse_color.texture) Tex = FbxMat->fbx.diffuse_color.texture;
		else if (FbxMat->fbx.diffuse_factor.texture) Tex = FbxMat->fbx.diffuse_factor.texture;
		else if (FbxMat->fbx.ambient_color.texture) Tex = FbxMat->fbx.ambient_color.texture;
		else
		{
			for (size_t t = 0; t < FbxMat->textures.count; ++t)
			{
				if (FbxMat->textures.data[t].texture)
				{
					Tex = FbxMat->textures.data[t].texture;
					break;
				}
			}
		}

		if (Tex)
		{
			if (Tex->content.size > 0)
			{
				ImportedMat.DiffuseTexture = LoadTextureFromMemory(
					(const uint8*)Tex->content.data,
					Tex->content.size
				);
			}
			else
			{
				TArray<FString> CandidatePaths;
				if (Tex->relative_filename.length > 0) CandidatePaths.Add(UTF8_TO_TCHAR(Tex->relative_filename.data));
				if (Tex->filename.length > 0) CandidatePaths.Add(UTF8_TO_TCHAR(Tex->filename.data));
				if (Tex->absolute_filename.length > 0) CandidatePaths.Add(UTF8_TO_TCHAR(Tex->absolute_filename.data));
				if (Tex->raw_filename.size > 0) CandidatePaths.Add(UTF8_TO_TCHAR((const char*)Tex->raw_filename.data));

				for (const FString& Candidate : CandidatePaths)
				{
					const FString Resolved = ResolveTexturePath(FilePath, Candidate);
					if (!Resolved.IsEmpty())
					{
						ImportedMat.DiffuseTexturePath = Resolved;
						ImportedMat.DiffuseTexture = LoadTextureFromFile(Resolved);
						if (ImportedMat.DiffuseTexture)
						{
							break;
						}
					}
				}
			}
		}

		// If no texture found on material slot, search companion texture by material/model name
		if (!ImportedMat.DiffuseTexture)
		{
			const FString ModelName = FPaths::GetBaseFilename(FilePath);
			const FString MatName = ImportedMat.MaterialName;
			static const TArray<FString> Suffixes = {
				TEXT(""), TEXT("_diffuse"), TEXT("_Diffuse"), TEXT("_basecolor"), TEXT("_BaseColor"),
				TEXT("_albedo"), TEXT("_Albedo"), TEXT("_D"), TEXT("_d"), TEXT("_color"), TEXT("_Color"),
				TEXT("_tex"), TEXT("_texture")
			};
			static const TArray<FString> Exts = { TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("bmp"), TEXT("tga") };

			for (const FString& NameStem : { MatName, ModelName })
			{
				if (NameStem.IsEmpty()) continue;
				for (const FString& Suffix : Suffixes)
				{
					for (const FString& Ext : Exts)
					{
						const FString TestFileName = NameStem + Suffix + TEXT(".") + Ext;
						const FString Resolved = ResolveTexturePath(FilePath, TestFileName);
						if (!Resolved.IsEmpty())
						{
							ImportedMat.DiffuseTexturePath = Resolved;
							ImportedMat.DiffuseTexture = LoadTextureFromFile(Resolved);
							if (ImportedMat.DiffuseTexture) break;
						}
					}
					if (ImportedMat.DiffuseTexture) break;
				}
				if (ImportedMat.DiffuseTexture) break;
			}
		}

		const int32 MatSlot = OutResult.Materials.Add(ImportedMat);
		MaterialToIndex.Add(FbxMat, MatSlot);
	}

	if (OutResult.Materials.Num() == 0)
	{
		FGRTImportedMaterial DefaultMat;
		DefaultMat.MaterialName = TEXT("DefaultMaterial");
		DefaultMat.DiffuseColor = FLinearColor(0.75f, 0.75f, 0.75f);
		OutResult.Materials.Add(DefaultMat);
	}

	// Iterate nodes / meshes in scene
	for (size_t n = 0; n < scene->nodes.count; ++n)
	{
		ufbx_node* Node = scene->nodes.data[n];
		if (!Node->mesh) continue;

		ufbx_mesh* FbxMesh = Node->mesh;
		const ufbx_matrix NodeToWorld = Node->geometry_to_world;

		for (size_t f = 0; f < FbxMesh->num_faces; ++f)
		{
			ufbx_face Face = FbxMesh->faces.data[f];
			if (Face.num_indices < 3) continue;

			int32 FaceMatID = 0;
			if (FbxMesh->face_material.count > f)
			{
				uint32_t MatIdx = FbxMesh->face_material.data[f];
				if (MatIdx < FbxMesh->materials.count)
				{
					ufbx_material* Mat = FbxMesh->materials.data[MatIdx];
					if (int32* FoundSlot = MaterialToIndex.Find(Mat))
					{
						FaceMatID = *FoundSlot;
					}
				}
			}

			// Fan triangulation
			for (uint32_t i = 2; i < Face.num_indices; ++i)
			{
				uint32_t i0 = Face.index_begin + 0;
				uint32_t i1 = Face.index_begin + i - 1;
				uint32_t i2 = Face.index_begin + i;

				uint32_t v_idx[3] = {
					FbxMesh->vertex_indices.data[i0],
					FbxMesh->vertex_indices.data[i1],
					FbxMesh->vertex_indices.data[i2]
				};

				ufbx_vec3 p0 = ufbx_transform_position(&NodeToWorld, FbxMesh->vertices.data[v_idx[0]]);
				ufbx_vec3 p1 = ufbx_transform_position(&NodeToWorld, FbxMesh->vertices.data[v_idx[1]]);
				ufbx_vec3 p2 = ufbx_transform_position(&NodeToWorld, FbxMesh->vertices.data[v_idx[2]]);

				FVector3d V0(p0.x * 100.0, p0.y * 100.0, p0.z * 100.0);
				FVector3d V1(p1.x * 100.0, p1.y * 100.0, p1.z * 100.0);
				FVector3d V2(p2.x * 100.0, p2.y * 100.0, p2.z * 100.0);

				int32 VId0 = Mesh.AppendVertex(V0);
				int32 VId1 = Mesh.AppendVertex(V1);
				int32 VId2 = Mesh.AppendVertex(V2);

				int32 TriID = Mesh.AppendTriangle(VId0, VId1, VId2);
				if (TriID >= 0)
				{
					if (MaterialAttr)
					{
						MaterialAttr->SetValue(TriID, FaceMatID);
					}

					if (UVOverlay && FbxMesh->vertex_uv.exists)
					{
						ufbx_vec2 uv0 = FbxMesh->vertex_uv.values.data[FbxMesh->vertex_uv.indices.data[i0]];
						ufbx_vec2 uv1 = FbxMesh->vertex_uv.values.data[FbxMesh->vertex_uv.indices.data[i1]];
						ufbx_vec2 uv2 = FbxMesh->vertex_uv.values.data[FbxMesh->vertex_uv.indices.data[i2]];

						int32 U0 = UVOverlay->AppendElement(FVector2f(uv0.x, 1.0f - uv0.y));
						int32 U1 = UVOverlay->AppendElement(FVector2f(uv1.x, 1.0f - uv1.y));
						int32 U2 = UVOverlay->AppendElement(FVector2f(uv2.x, 1.0f - uv2.y));

						UVOverlay->SetTriangle(TriID, FIndex3i(U0, U1, U2));
					}

					if (NormalOverlay && FbxMesh->vertex_normal.exists)
					{
						ufbx_vec3 n0 = ufbx_transform_direction(&NodeToWorld, FbxMesh->vertex_normal.values.data[FbxMesh->vertex_normal.indices.data[i0]]);
						ufbx_vec3 n1 = ufbx_transform_direction(&NodeToWorld, FbxMesh->vertex_normal.values.data[FbxMesh->vertex_normal.indices.data[i1]]);
						ufbx_vec3 n2 = ufbx_transform_direction(&NodeToWorld, FbxMesh->vertex_normal.values.data[FbxMesh->vertex_normal.indices.data[i2]]);

						int32 N0 = NormalOverlay->AppendElement(FVector3f(n0.x, n0.y, n0.z));
						int32 N1 = NormalOverlay->AppendElement(FVector3f(n1.x, n1.y, n1.z));
						int32 N2 = NormalOverlay->AppendElement(FVector3f(n2.x, n2.y, n2.z));

						NormalOverlay->SetTriangle(TriID, FIndex3i(N0, N1, N2));
					}
				}
			}
		}
	}

	ufbx_free_scene(scene);

	if (Mesh.TriangleCount() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("FBX file contained no valid triangles.");
		return false;
	}

	if (NormalOverlay && NormalOverlay->ElementCount() == 0)
	{
		FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
	}

	OutResult.bSuccess = true;
	return true;
}

bool FGRTModelImporter::ImportGLTF(const FString& FilePath, FGRTImportModelResult& OutResult)
{
	cgltf_options options = {};
	cgltf_data* data = nullptr;

	cgltf_result parse_res = cgltf_parse_file(&options, TCHAR_TO_UTF8(*FilePath), &data);
	if (parse_res != cgltf_result_success || !data)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to parse glTF/GLB file: %s (code %d)"), *FilePath, (int32)parse_res);
		return false;
	}

	cgltf_result buf_res = cgltf_load_buffers(&options, data, TCHAR_TO_UTF8(*FilePath));
	if (buf_res != cgltf_result_success)
	{
		cgltf_free(data);
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to load glTF buffers for: %s (code %d)"), *FilePath, (int32)buf_res);
		return false;
	}

	FDynamicMesh3& Mesh = OutResult.Mesh;
	Mesh.Clear();
	Mesh.EnableAttributes();
	Mesh.Attributes()->EnableMaterialID();
	FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
	FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
	FDynamicMeshMaterialAttribute* MaterialAttr = Mesh.Attributes()->GetMaterialID();

	const FString BaseDir = FPaths::GetPath(FilePath);

	TMap<const cgltf_material*, int32> MaterialToIndex;
	for (size_t m = 0; m < data->materials_count; ++m)
	{
		const cgltf_material& GMat = data->materials[m];
		FGRTImportedMaterial ImportedMat;
		ImportedMat.MaterialName = GMat.name ? UTF8_TO_TCHAR(GMat.name) : FString::Printf(TEXT("Material_%d"), (int32)m);

		if (GMat.has_pbr_metallic_roughness)
		{
			ImportedMat.DiffuseColor = FLinearColor(
				GMat.pbr_metallic_roughness.base_color_factor[0],
				GMat.pbr_metallic_roughness.base_color_factor[1],
				GMat.pbr_metallic_roughness.base_color_factor[2],
				GMat.pbr_metallic_roughness.base_color_factor[3]
			);

			if (GMat.pbr_metallic_roughness.base_color_texture.texture &&
				GMat.pbr_metallic_roughness.base_color_texture.texture->image)
			{
				const cgltf_image* Image = GMat.pbr_metallic_roughness.base_color_texture.texture->image;
				if (Image->buffer_view && Image->buffer_view->buffer && Image->buffer_view->buffer->data)
				{
					const uint8* ImgBytes = (const uint8*)Image->buffer_view->buffer->data + Image->buffer_view->offset;
					const int64 ImgSize = Image->buffer_view->size;
					FString MimeType = Image->mime_type ? UTF8_TO_TCHAR(Image->mime_type) : TEXT("png");
					ImportedMat.DiffuseTexture = LoadTextureFromMemory(ImgBytes, ImgSize, MimeType);
				}
				else if (Image->uri)
				{
					FString TexPath = UTF8_TO_TCHAR(Image->uri);
					FString ResolvedTexPath = ResolveTexturePath(FilePath, TexPath);
					if (!ResolvedTexPath.IsEmpty())
					{
						ImportedMat.DiffuseTexturePath = ResolvedTexPath;
						ImportedMat.DiffuseTexture = LoadTextureFromFile(ResolvedTexPath);
					}
				}
			}
		}

		const int32 MatSlot = OutResult.Materials.Add(ImportedMat);
		MaterialToIndex.Add(&GMat, MatSlot);
	}

	if (OutResult.Materials.Num() == 0)
	{
		FGRTImportedMaterial DefaultMat;
		DefaultMat.MaterialName = TEXT("DefaultMaterial");
		DefaultMat.DiffuseColor = FLinearColor(0.75f, 0.75f, 0.75f);
		OutResult.Materials.Add(DefaultMat);
	}

	for (size_t m = 0; m < data->meshes_count; ++m)
	{
		const cgltf_mesh& GMesh = data->meshes[m];
		for (size_t p = 0; p < GMesh.primitives_count; ++p)
		{
			const cgltf_primitive& Prim = GMesh.primitives[p];
			if (Prim.type != cgltf_primitive_type_triangles)
			{
				continue;
			}

			const cgltf_accessor* PosAccessor = nullptr;
			const cgltf_accessor* NormAccessor = nullptr;
			const cgltf_accessor* UVAccessor = nullptr;

			for (size_t a = 0; a < Prim.attributes_count; ++a)
			{
				const cgltf_attribute& Attr = Prim.attributes[a];
				if (Attr.type == cgltf_attribute_type_position) PosAccessor = Attr.data;
				else if (Attr.type == cgltf_attribute_type_normal) NormAccessor = Attr.data;
				else if (Attr.type == cgltf_attribute_type_texcoord && Attr.index == 0) UVAccessor = Attr.data;
			}

			if (!PosAccessor) continue;

			int32 PrimMatID = 0;
			if (Prim.material)
			{
				if (int32* FoundSlot = MaterialToIndex.Find(Prim.material))
				{
					PrimMatID = *FoundSlot;
				}
			}

			const size_t NumIndices = Prim.indices ? Prim.indices->count : PosAccessor->count;
			for (size_t i = 0; i + 2 < NumIndices; i += 3)
			{
				size_t idx0 = Prim.indices ? cgltf_accessor_read_index(Prim.indices, i + 0) : (i + 0);
				size_t idx1 = Prim.indices ? cgltf_accessor_read_index(Prim.indices, i + 1) : (i + 1);
				size_t idx2 = Prim.indices ? cgltf_accessor_read_index(Prim.indices, i + 2) : (i + 2);

				float p0[3] = {0}, p1[3] = {0}, p2[3] = {0};
				cgltf_accessor_read_float(PosAccessor, idx0, p0, 3);
				cgltf_accessor_read_float(PosAccessor, idx1, p1, 3);
				cgltf_accessor_read_float(PosAccessor, idx2, p2, 3);

				// glTF to Unreal cm conversion: (+X Right, +Y Up, +Z Forward) -> (+X Forward, +Y Right, +Z Up)
				FVector3d V0(p0[0] * 100.0, -p0[2] * 100.0, p0[1] * 100.0);
				FVector3d V1(p1[0] * 100.0, -p1[2] * 100.0, p1[1] * 100.0);
				FVector3d V2(p2[0] * 100.0, -p2[2] * 100.0, p2[1] * 100.0);

				int32 VId0 = Mesh.AppendVertex(V0);
				int32 VId1 = Mesh.AppendVertex(V1);
				int32 VId2 = Mesh.AppendVertex(V2);

				int32 TriID = Mesh.AppendTriangle(VId0, VId1, VId2);
				if (TriID >= 0)
				{
					if (MaterialAttr)
					{
						MaterialAttr->SetValue(TriID, PrimMatID);
					}

					if (UVOverlay && UVAccessor)
					{
						float uv0[2] = {0}, uv1[2] = {0}, uv2[2] = {0};
						cgltf_accessor_read_float(UVAccessor, idx0, uv0, 2);
						cgltf_accessor_read_float(UVAccessor, idx1, uv1, 2);
						cgltf_accessor_read_float(UVAccessor, idx2, uv2, 2);

						int32 U0 = UVOverlay->AppendElement(FVector2f(uv0[0], uv0[1]));
						int32 U1 = UVOverlay->AppendElement(FVector2f(uv1[0], uv1[1]));
						int32 U2 = UVOverlay->AppendElement(FVector2f(uv2[0], uv2[1]));

						UVOverlay->SetTriangle(TriID, FIndex3i(U0, U1, U2));
					}

					if (NormalOverlay && NormAccessor)
					{
						float n0[3] = {0}, n1[3] = {0}, n2[3] = {0};
						cgltf_accessor_read_float(NormAccessor, idx0, n0, 3);
						cgltf_accessor_read_float(NormAccessor, idx1, n1, 3);
						cgltf_accessor_read_float(NormAccessor, idx2, n2, 3);

						FVector3f N0(n0[0], -n0[2], n0[1]);
						FVector3f N1(n1[0], -n1[2], n1[1]);
						FVector3f N2(n2[0], -n2[2], n2[1]);

						int32 NId0 = NormalOverlay->AppendElement(N0);
						int32 NId1 = NormalOverlay->AppendElement(N1);
						int32 NId2 = NormalOverlay->AppendElement(N2);

						NormalOverlay->SetTriangle(TriID, FIndex3i(NId0, NId1, NId2));
					}
				}
			}
		}
	}

	cgltf_free(data);

	if (Mesh.TriangleCount() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("glTF/GLB file contained no valid triangles.");
		return false;
	}

	if (NormalOverlay && NormalOverlay->ElementCount() == 0)
	{
		FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
	}

	OutResult.bSuccess = true;
	return true;
}
