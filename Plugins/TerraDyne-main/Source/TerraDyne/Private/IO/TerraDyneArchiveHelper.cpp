// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneArchiveHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace
{
	bool HasTiffExt(const FString& Path)
	{
		const FString Ext = FPaths::GetExtension(Path);
		return Ext.Equals(TEXT("tif"), ESearchCase::IgnoreCase) || Ext.Equals(TEXT("tiff"), ESearchCase::IgnoreCase);
	}

	FString GetLayerKeyword(ETerraDyneRasterLayer Layer)
	{
		switch (Layer)
		{
		case ETerraDyneRasterLayer::Elevation:      return TEXT("dem");
		case ETerraDyneRasterLayer::Hillshade:      return TEXT("hillshade");
		case ETerraDyneRasterLayer::ColorRelief:    return TEXT("color-relief");
		case ETerraDyneRasterLayer::ColorHillshade: return TEXT("hillshade-color");
		case ETerraDyneRasterLayer::Slope:          return TEXT("slope");
		case ETerraDyneRasterLayer::Aspect:         return TEXT("aspect");
		case ETerraDyneRasterLayer::Roughness:      return TEXT("roughness");
		default:                                    return FString();
		}
	}
}

bool FTerraDyneArchiveHelper::IsArchiveFile(const FString& FilePath)
{
	const FString Lower = FilePath.ToLower();
	return Lower.EndsWith(TEXT(".tar.gz"))
		|| Lower.EndsWith(TEXT(".tgz"))
		|| Lower.EndsWith(TEXT(".zip"))
		|| Lower.EndsWith(TEXT(".tar"));
}

bool FTerraDyneArchiveHelper::HasSupportedExtension(const FString& FilePath)
{
	if (HasTiffExt(FilePath))
	{
		return true;
	}
	return IsArchiveFile(FilePath);
}

bool FTerraDyneArchiveHelper::ResolveOrExtractRaster(
	const FString& InputPath,
	ETerraDyneRasterLayer Layer,
	FString& OutResolvedPath,
	FString& OutError)
{
	OutResolvedPath.Reset();
	OutError.Reset();

	if (InputPath.IsEmpty())
	{
		OutError = TEXT("No input file specified.");
		return false;
	}

	if (!FPaths::FileExists(InputPath))
	{
		OutError = FString::Printf(TEXT("File does not exist: %s"), *InputPath);
		return false;
	}

	if (HasTiffExt(InputPath))
	{
		OutResolvedPath = InputPath;
		return true;
	}

	if (!IsArchiveFile(InputPath))
	{
		OutError = FString::Printf(TEXT("Unsupported file format: '%s'. Expected .tif, .tiff, .tar.gz, .tgz, or .zip."),
			*FPaths::GetCleanFilename(InputPath));
		return false;
	}

	// It is an archive: extract to Saved/ExtractedRasters/<ArchiveBaseName>
	const FString CleanName = FPaths::GetBaseFilename(InputPath);
	const FString BaseName = CleanName.EndsWith(TEXT(".tar"), ESearchCase::IgnoreCase)
		? FPaths::GetBaseFilename(CleanName)
		: CleanName;

	FString NativeDestDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ExtractedRasters"), BaseName));
	FPaths::MakePlatformFilename(NativeDestDir);
	IFileManager::Get().MakeDirectory(*NativeDestDir, true);

	// Check if already extracted
	TArray<FString> ExistingTifs;
	IFileManager::Get().FindFilesRecursive(ExistingTifs, *NativeDestDir, TEXT("*.tif"), true, false, false);
	IFileManager::Get().FindFilesRecursive(ExistingTifs, *NativeDestDir, TEXT("*.tiff"), true, false, false);

	if (ExistingTifs.Num() == 0)
	{
		// Extract using Windows system tar
		const FString TarExe = FPaths::FileExists(TEXT("C:\\Windows\\System32\\tar.exe"))
			? TEXT("C:\\Windows\\System32\\tar.exe")
			: TEXT("tar.exe");

		FString NativeInputPath = FPaths::ConvertRelativePathToFull(InputPath);
		FPaths::MakePlatformFilename(NativeInputPath);

		const FString Params = FString::Printf(TEXT("-xf \"%s\" -C \"%s\""), *NativeInputPath, *NativeDestDir);

		int32 ReturnCode = 0;
		FString StdOut;
		FString StdErr;
		const bool bSuccess = FPlatformProcess::ExecProcess(*TarExe, *Params, &ReturnCode, &StdOut, &StdErr);

		if (!bSuccess || ReturnCode != 0)
		{
			OutError = FString::Printf(TEXT("Failed to extract archive '%s' (code %d): %s %s"),
				*FPaths::GetCleanFilename(InputPath), ReturnCode, *StdErr, *StdOut);
			return false;
		}

		// Re-search extracted directory
		IFileManager::Get().FindFilesRecursive(ExistingTifs, *NativeDestDir, TEXT("*.tif"), true, false, false);
		IFileManager::Get().FindFilesRecursive(ExistingTifs, *NativeDestDir, TEXT("*.tiff"), true, false, false);
	}

	if (ExistingTifs.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No .tif or .tiff rasters found inside archive '%s'."),
			*FPaths::GetCleanFilename(InputPath));
		return false;
	}

	// If there are multiple tifs, pick the one best matching the layer keyword
	const FString Keyword = GetLayerKeyword(Layer);
	if (!Keyword.IsEmpty() && ExistingTifs.Num() > 1)
	{
		for (const FString& TifPath : ExistingTifs)
		{
			if (FPaths::GetCleanFilename(TifPath).Contains(Keyword, ESearchCase::IgnoreCase))
			{
				OutResolvedPath = TifPath;
				return true;
			}
		}
	}

	// For Elevation specifically, if "output" is in filename, prefer it
	if (Layer == ETerraDyneRasterLayer::Elevation && ExistingTifs.Num() > 1)
	{
		for (const FString& TifPath : ExistingTifs)
		{
			if (FPaths::GetCleanFilename(TifPath).Contains(TEXT("output"), ESearchCase::IgnoreCase))
			{
				OutResolvedPath = TifPath;
				return true;
			}
		}
	}

	// Default to first found
	OutResolvedPath = ExistingTifs[0];
	return true;
}
