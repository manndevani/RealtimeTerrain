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
		SLATE_EVENT(FTerraDyneFilePickerChosen, OnFileChosen)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FString CurrentDirectory;
	FString SelectedFilePath;
	TArray<FString> Subdirectories;
	TArray<FString> Files;

	FTerraDyneFilePickerChosen OnFileChosenDelegate;
	FSimpleDelegate OnCancelledDelegate;

	TSharedPtr<class SVerticalBox> ListContainer;
	FButtonStyle HudButtonStyle;
	FButtonStyle HudEntryStyle;

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
