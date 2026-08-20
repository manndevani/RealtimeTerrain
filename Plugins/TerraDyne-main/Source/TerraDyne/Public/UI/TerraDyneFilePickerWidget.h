// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TerraDyneFilePickerWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTerraDyneOnGeoTIFFChosen, const FString&, FilePath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTerraDyneOnFilePickerCancelled);

/**
 * Runtime, packaged-build-safe directory/file browser for picking the .tif/.tiff to import in
 * Imported world mode. Built on IFileManager::IterateDirectory rather than the OS Open File dialog
 * (IDesktopPlatform::OpenFileDialog, which TerraDyne.Build.cs only links into WITH_EDITOR builds --
 * see the "DesktopPlatform" comment there), so this same widget works in a packaged build too.
 *
 * Pure native Slate underneath (STerraDyneFilePickerPanel, which owns all browsing state itself --
 * navigation, selection, the visible list), the same split STerraDynePanel already uses for the
 * main tool UI. This class is just the UUserWidget host: it hands the panel a starting directory
 * and re-broadcasts its two outcomes as Blueprint-assignable delegates.
 */
UCLASS()
class TERRADYNE_API UTerraDyneFilePickerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Directory shown when the widget first appears. Empty resolves to FPlatformProcess::UserDir(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|FilePicker")
	FString StartingDirectory;

	/** Fired once, with the chosen file's full path, when the player confirms a selection. */
	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|FilePicker")
	FTerraDyneOnGeoTIFFChosen OnFileChosen;

	/** Fired if the player dismisses the picker without choosing a file. */
	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|FilePicker")
	FTerraDyneOnFilePickerCancelled OnCancelled;

private:
	TSharedPtr<class STerraDyneFilePickerPanel> Panel;

	void HandlePanelFileChosen(FString FullPath);
	void HandlePanelCancelled();
};
