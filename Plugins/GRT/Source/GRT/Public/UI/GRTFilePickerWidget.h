// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GRTFilePickerWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGRTOnGeoTIFFChosen, const FString&, FilePath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGRTOnFilePickerCancelled);

/**
 * Runtime, packaged-build-safe directory/file browser for picking the .tif/.tiff to import in
 * Imported world mode. Built on IFileManager::IterateDirectory rather than the OS Open File dialog
 * (IDesktopPlatform::OpenFileDialog, which GRT.Build.cs only links into WITH_EDITOR builds --
 * see the "DesktopPlatform" comment there), so this same widget works in a packaged build too.
 *
 * Pure native Slate underneath (SGRTFilePickerPanel, which owns all browsing state itself --
 * navigation, selection, the visible list), the same split SGRTPanel already uses for the
 * main tool UI. This class is just the UUserWidget host: it hands the panel a starting directory
 * and re-broadcasts its two outcomes as Blueprint-assignable delegates.
 */
UCLASS()
class GRT_API UGRTFilePickerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Directory shown when the widget first appears. Empty resolves to FPlatformProcess::UserDir(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|FilePicker")
	FString StartingDirectory;

	/** Extensions to list, without dots. Empty keeps the raster set this picker originally shipped with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|FilePicker")
	TArray<FString> AllowedExtensions;

	/** Replaces the panel's title line. Empty keeps the GeoTIFF wording. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|FilePicker")
	FText TitleOverride;

	/** Fired once, with the chosen file's full path, when the player confirms a selection. */
	UPROPERTY(BlueprintAssignable, Category = "GRT|FilePicker")
	FGRTOnGeoTIFFChosen OnFileChosen;

	/** Fired if the player dismisses the picker without choosing a file. */
	UPROPERTY(BlueprintAssignable, Category = "GRT|FilePicker")
	FGRTOnFilePickerCancelled OnCancelled;

private:
	TSharedPtr<class SGRTFilePickerPanel> Panel;

	void HandlePanelFileChosen(FString FullPath);
	void HandlePanelCancelled();
};
