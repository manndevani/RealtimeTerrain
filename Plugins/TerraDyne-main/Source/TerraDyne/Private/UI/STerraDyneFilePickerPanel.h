// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

DECLARE_DELEGATE_OneParam(FTerraDyneFilePickerChosen, FString /*FullPath*/);

/**
 * Native Slate implementation of the runtime GeoTIFF file browser. See
 * UTerraDyneFilePickerWidget for why this exists instead of the OS Open File dialog.
 */
class STerraDyneFilePickerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STerraDyneFilePickerPanel) {}
		SLATE_ARGUMENT(FString, StartingDirectory)
		/** Extensions to list, without dots and case-insensitive. Empty keeps the raster default. */
		SLATE_ARGUMENT(TArray<FString>, AllowedExtensions)
		/** Replaces the panel's title line. Empty keeps the GeoTIFF wording. */
		SLATE_ARGUMENT(FText, TitleOverride)
		SLATE_EVENT(FTerraDyneFilePickerChosen, OnFileChosen)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/**
	 * When empty this panel lists whatever FTerraDyneArchiveHelper accepts, which is what every
	 * raster caller wants. A caller that supplies extensions gets exactly those instead -- the panel
	 * used to hardcode the raster set, so it could not be reused for anything else.
	 */
	TArray<FString> AllowedExtensions;

	FString CurrentDirectory;
	FString SelectedFilePath;
	TArray<FString> Subdirectories;
	TArray<FString> Files;

	FTerraDyneFilePickerChosen OnFileChosenDelegate;
	FSimpleDelegate OnCancelledDelegate;

	TSharedPtr<class SVerticalBox> ListContainer;
	FButtonStyle HudButtonStyle;
	FButtonStyle HudEntryStyle;

	/** True when Filename should appear in the listing, honouring AllowedExtensions. */
	bool IsAcceptedFile(const FString& Filename) const;

	void NavigateToDirectory(const FString& Path);
	void RefreshListing();
	void RebuildListWidgets();

	FReply OnUpClicked();
	FReply OnEntryClicked(FString FullPath, bool bIsDirectory);
	FReply OnImportClicked();
	FReply OnCancelClicked();

	FText GetCurrentDirectoryText() const;
	FText GetSelectedFileText() const;
	bool IsImportEnabled() const;
	FSlateColor GetEntryColor(FString FullPath) const;
	FSlateColor GetSelectedFileColor() const;
	FSlateColor GetImportLabelColor() const;
};
