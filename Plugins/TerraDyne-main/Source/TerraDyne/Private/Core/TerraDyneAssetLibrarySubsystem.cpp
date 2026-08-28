// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneAssetLibrarySubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TerraDyneModule.h"

void UTerraDyneAssetLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadedAssets.Reset();
	AssetIdToIndex.Reset();
	SelectedPlacementAssetId.Invalidate();
}

void UTerraDyneAssetLibrarySubsystem::Deinitialize()
{
	LoadedAssets.Reset();
	AssetIdToIndex.Reset();
	Super::Deinitialize();
}

FGuid UTerraDyneAssetLibrarySubsystem::ImportModelFromFile(const FString& FilePath, FString& OutError)
{
	FTerraDyneImportModelResult Result = FTerraDyneModelImporter::ImportModelFromFile(FilePath);
	if (!Result.bSuccess)
	{
		OutError = Result.ErrorMessage;
		UE_LOG(LogTerraDyne, Warning, TEXT("Failed to import model '%s': %s"), *FilePath, *Result.ErrorMessage);
		return FGuid();
	}

	FTerraDyneImportedModelAsset NewAsset;
	NewAsset.AssetId = FGuid::NewGuid();
	NewAsset.AssetName = Result.ModelName;
	NewAsset.SourceFilePath = FilePath;
	NewAsset.Mesh = MoveTemp(Result.Mesh);
	NewAsset.Materials = MoveTemp(Result.Materials);
	NewAsset.BoundingBox = Result.BoundingBox;
	NewAsset.VertexCount = Result.VertexCount;
	NewAsset.TriangleCount = Result.TriangleCount;

	const FVector3d Extents = Result.BoundingBox.GetExtent();
	NewAsset.Dimensions = FVector(Extents.X * 2.0, Extents.Y * 2.0, Extents.Z * 2.0);

	TryFindThumbnailForAsset(NewAsset);

	const int32 Index = LoadedAssets.Add(MoveTemp(NewAsset));
	AssetIdToIndex.Add(LoadedAssets[Index].AssetId, Index);

	if (!SelectedPlacementAssetId.IsValid())
	{
		SelectedPlacementAssetId = LoadedAssets[Index].AssetId;
	}

	OnAssetLibraryChanged.Broadcast();
	return LoadedAssets[Index].AssetId;
}

void UTerraDyneAssetLibrarySubsystem::TryFindThumbnailForAsset(FTerraDyneImportedModelAsset& Asset)
{
	const FString Dir = FPaths::GetPath(Asset.SourceFilePath);
	const FString BaseName = FPaths::GetBaseFilename(Asset.SourceFilePath);

	const TArray<FString> CandidateNames = {
		BaseName + TEXT(".png"),
		BaseName + TEXT(".jpg"),
		BaseName + TEXT("_thumb.png"),
		BaseName + TEXT("_preview.png"),
		TEXT("thumbnail.png"),
		TEXT("preview.png")
	};

	for (const FString& Candidate : CandidateNames)
	{
		const FString CandidatePath = FPaths::Combine(Dir, Candidate);
		if (IFileManager::Get().FileExists(*CandidatePath))
		{
			UTexture2D* LoadedTex = FTerraDyneModelImporter::LoadTextureFromFile(CandidatePath);
			if (LoadedTex)
			{
				Asset.ThumbnailTexture = LoadedTex;
				break;
			}
		}
	}
}

int32 UTerraDyneAssetLibrarySubsystem::ImportDirectory(const FString& DirectoryPath)
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *DirectoryPath, true, false);

	int32 ImportedCount = 0;
	for (const FString& FileName : FoundFiles)
	{
		const FString Ext = FPaths::GetExtension(FileName);
		if (FTerraDyneModelImporter::IsSupportedExtension(Ext))
		{
			const FString FullPath = FPaths::Combine(DirectoryPath, FileName);
			FString Error;
			if (ImportModelFromFile(FullPath, Error).IsValid())
			{
				ImportedCount++;
			}
		}
	}

	return ImportedCount;
}

bool UTerraDyneAssetLibrarySubsystem::RemoveAsset(const FGuid& AssetId)
{
	if (const int32* FoundIdx = AssetIdToIndex.Find(AssetId))
	{
		const int32 RemovedIdx = *FoundIdx;
		LoadedAssets.RemoveAt(RemovedIdx);

		// Rebuild index map
		AssetIdToIndex.Reset();
		for (int32 i = 0; i < LoadedAssets.Num(); ++i)
		{
			AssetIdToIndex.Add(LoadedAssets[i].AssetId, i);
		}

		if (SelectedPlacementAssetId == AssetId)
		{
			SelectedPlacementAssetId = LoadedAssets.Num() > 0 ? LoadedAssets[0].AssetId : FGuid();
		}

		OnAssetLibraryChanged.Broadcast();
		return true;
	}
	return false;
}

const FTerraDyneImportedModelAsset* UTerraDyneAssetLibrarySubsystem::FindAsset(const FGuid& AssetId) const
{
	if (const int32* FoundIdx = AssetIdToIndex.Find(AssetId))
	{
		if (LoadedAssets.IsValidIndex(*FoundIdx))
		{
			return &LoadedAssets[*FoundIdx];
		}
	}
	return nullptr;
}
