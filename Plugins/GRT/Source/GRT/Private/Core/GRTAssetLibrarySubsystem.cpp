// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/GRTAssetLibrarySubsystem.h"
#include "IO/GRTModelThumbnail.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "GRTModule.h"

void UGRTAssetLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadedAssets.Reset();
	AssetIdToIndex.Reset();
	SelectedPlacementAssetId.Invalidate();
}

void UGRTAssetLibrarySubsystem::Deinitialize()
{
	LoadedAssets.Reset();
	AssetIdToIndex.Reset();
	Super::Deinitialize();
}

FString UGRTAssetLibrarySubsystem::NormalizeSourcePath(const FString& FilePath)
{
	return FPaths::ConvertRelativePathToFull(FilePath).ToLower();
}

void UGRTAssetLibrarySubsystem::RebuildAssetIndex()
{
	AssetIdToIndex.Reset();
	for (int32 i = 0; i < LoadedAssets.Num(); ++i)
	{
		AssetIdToIndex.Add(LoadedAssets[i].AssetId, i);
	}
}

FGuid UGRTAssetLibrarySubsystem::ImportModelFromFile(const FString& FilePath, FString& OutError, const FGuid& InAssetId)
{
	// Importing the same file twice used to mint a second catalog entry with a new id, so the
	// palette filled up with duplicates and a save/reload could bind a placement to whichever copy
	// happened to come back first.
	if (const FGRTImportedModelAsset* Existing = FindAssetBySourcePath(FilePath))
	{
		if (!InAssetId.IsValid() || InAssetId == Existing->AssetId)
		{
			return Existing->AssetId;
		}
	}

	FGRTImportModelResult Result = FGRTModelImporter::ImportModelFromFile(FilePath);
	if (!Result.bSuccess)
	{
		OutError = Result.ErrorMessage;
		UE_LOG(LogGRT, Warning, TEXT("Failed to import model '%s': %s"), *FilePath, *Result.ErrorMessage);
		return FGuid();
	}

	FGRTImportedModelAsset NewAsset;
	NewAsset.AssetId = InAssetId.IsValid() ? InAssetId : FGuid::NewGuid();
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

void UGRTAssetLibrarySubsystem::TryFindThumbnailForAsset(FGRTImportedModelAsset& Asset)
{
	// A hand-authored image beside the model always wins -- an author who supplied artwork meant it
	// to be used, and it will read better than anything generated here.
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
			if (UTexture2D* LoadedTex = FGRTModelImporter::LoadTextureFromFile(CandidatePath))
			{
				Asset.ThumbnailTexture = LoadedTex;
				return;
			}
		}
	}

	// Otherwise render the geometry. Returns null with no RHI (headless), leaving the palette's
	// placeholder in place rather than a black tile.
	Asset.ThumbnailTexture = FGRTModelThumbnail::GetOrRender(GetWorld(), Asset.SourceFilePath, Asset.Mesh);
}

int32 UGRTAssetLibrarySubsystem::ImportDirectory(const FString& DirectoryPath)
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *DirectoryPath, true, false);

	int32 ImportedCount = 0;
	for (const FString& FileName : FoundFiles)
	{
		const FString Ext = FPaths::GetExtension(FileName);
		if (FGRTModelImporter::IsSupportedExtension(Ext))
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

bool UGRTAssetLibrarySubsystem::RemoveAsset(const FGuid& AssetId)
{
	if (const int32* FoundIdx = AssetIdToIndex.Find(AssetId))
	{
		LoadedAssets.RemoveAt(*FoundIdx);
		RebuildAssetIndex();

		if (SelectedPlacementAssetId == AssetId)
		{
			SelectedPlacementAssetId = LoadedAssets.Num() > 0 ? LoadedAssets[0].AssetId : FGuid();
		}

		OnAssetLibraryChanged.Broadcast();
		return true;
	}
	return false;
}

void UGRTAssetLibrarySubsystem::ClearLibrary()
{
	LoadedAssets.Reset();
	AssetIdToIndex.Reset();
	SelectedPlacementAssetId.Invalidate();
	OnAssetLibraryChanged.Broadcast();
}

const FGRTImportedModelAsset* UGRTAssetLibrarySubsystem::FindAsset(const FGuid& AssetId) const
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

const FGRTImportedModelAsset* UGRTAssetLibrarySubsystem::FindAssetBySourcePath(const FString& FilePath) const
{
	const FString Normalized = NormalizeSourcePath(FilePath);
	for (const FGRTImportedModelAsset& Asset : LoadedAssets)
	{
		if (NormalizeSourcePath(Asset.SourceFilePath) == Normalized)
		{
			return &Asset;
		}
	}
	return nullptr;
}

const FGRTImportedModelAsset* UGRTAssetLibrarySubsystem::ResolveOrImport(const FGuid& AssetId, const FString& SourceFilePath, FString& OutError)
{
	if (const FGRTImportedModelAsset* Found = FindAsset(AssetId))
	{
		return Found;
	}

	if (SourceFilePath.IsEmpty())
	{
		OutError = TEXT("No source file path was replicated with this model, so it cannot be imported here.");
		return nullptr;
	}

	if (!IFileManager::Get().FileExists(*SourceFilePath))
	{
		OutError = FString::Printf(TEXT("Model source '%s' does not exist on this machine."), *SourceFilePath);
		return nullptr;
	}

	const FGuid Imported = ImportModelFromFile(SourceFilePath, OutError, AssetId);
	return Imported.IsValid() ? FindAsset(Imported) : nullptr;
}

TArray<FGRTModelLibraryEntryData> UGRTAssetLibrarySubsystem::ExportLibrary() const
{
	TArray<FGRTModelLibraryEntryData> Entries;
	Entries.Reserve(LoadedAssets.Num());
	for (const FGRTImportedModelAsset& Asset : LoadedAssets)
	{
		FGRTModelLibraryEntryData Entry;
		Entry.AssetId = Asset.AssetId;
		Entry.AssetName = Asset.AssetName;
		Entry.SourceFilePath = Asset.SourceFilePath;
		Entries.Add(MoveTemp(Entry));
	}
	return Entries;
}

int32 UGRTAssetLibrarySubsystem::RestoreLibrary(const TArray<FGRTModelLibraryEntryData>& Entries)
{
	ClearLibrary();

	int32 Restored = 0;
	for (const FGRTModelLibraryEntryData& Entry : Entries)
	{
		FString Error;
		if (ImportModelFromFile(Entry.SourceFilePath, Error, Entry.AssetId).IsValid())
		{
			++Restored;
		}
		else
		{
			// A missing source is expected and survivable -- the model was moved or the save came
			// from another machine. Say so once per entry and carry on with the rest.
			UE_LOG(LogGRT, Warning, TEXT("GRT: could not restore model '%s' from '%s': %s"),
				*Entry.AssetName, *Entry.SourceFilePath, *Error);
		}
	}

	OnAssetLibraryChanged.Broadcast();
	return Restored;
}
