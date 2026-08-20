// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDyneFilePickerPanel.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "TerraDyneFilePicker"

namespace
{
	bool HasTiffExtension(const FString& Filename)
	{
		const FString Ext = FPaths::GetExtension(Filename);
		return Ext.Equals(TEXT("tif"), ESearchCase::IgnoreCase) || Ext.Equals(TEXT("tiff"), ESearchCase::IgnoreCase);
	}
}

void STerraDyneFilePickerPanel::Construct(const FArguments& InArgs)
{
	OnFileChosenDelegate = InArgs._OnFileChosen;
	OnCancelledDelegate = InArgs._OnCancelled;

	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 14);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FLinearColor BGColor(0.05f, 0.05f, 0.05f, 0.95f);
	const FLinearColor SectionBGColor(0.1f, 0.1f, 0.1f, 0.9f);

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(520.0f)
		.HeightOverride(420.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(BGColor)
			.Padding(10.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Select a GeoTIFF (.tif / .tiff)"))
					.Font(HeaderFont)
				]

				// Current directory + Up button
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("Up", "Up"))
						.OnClicked(this, &STerraDyneFilePickerPanel::OnUpClicked)
					]
					+ SHorizontalBox::Slot().VAlign(VAlign_Center).FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(this, &STerraDyneFilePickerPanel::GetCurrentDirectoryText)
						.Font(LabelFont)
						.AutoWrapText(true)
					]
				]

				// Listing
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 0, 0, 6)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(SectionBGColor)
					.Padding(4.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(ListContainer, SVerticalBox)
						]
					]
				]

				// Selected file + Import/Cancel
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(STextBlock)
						.Text(this, &STerraDyneFilePickerPanel::GetSelectedFileText)
						.Font(LabelFont)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("Import", "Import"))
							.IsEnabled(this, &STerraDyneFilePickerPanel::IsImportEnabled)
							.OnClicked(this, &STerraDyneFilePickerPanel::OnImportClicked)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("Cancel", "Cancel"))
							.OnClicked(this, &STerraDyneFilePickerPanel::OnCancelClicked)
						]
					]
				]
			]
		]
	];

	FString Start = InArgs._StartingDirectory;
	if (Start.IsEmpty())
	{
		Start = FPlatformProcess::UserDir();
	}
	NavigateToDirectory(Start);
}

void STerraDyneFilePickerPanel::NavigateToDirectory(const FString& Path)
{
	FString Normalized = Path;
	FPaths::NormalizeDirectoryName(Normalized);
	if (!FPaths::DirectoryExists(Normalized))
	{
		Normalized = FPlatformProcess::UserDir();
	}

	CurrentDirectory = Normalized;
	SelectedFilePath.Empty();
	RefreshListing();
}

void STerraDyneFilePickerPanel::RefreshListing()
{
	Subdirectories.Reset();
	Files.Reset();

	IFileManager::Get().IterateDirectory(*CurrentDirectory, [this](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		const FString Entry(FilenameOrDirectory);
		if (bIsDirectory)
		{
			Subdirectories.Add(Entry);
		}
		else if (HasTiffExtension(Entry))
		{
			Files.Add(Entry);
		}
		return true;
	});

	Subdirectories.Sort();
	Files.Sort();

	RebuildListWidgets();
}

void STerraDyneFilePickerPanel::RebuildListWidgets()
{
	if (!ListContainer.IsValid())
	{
		return;
	}

	ListContainer->ClearChildren();

	for (const FString& Dir : Subdirectories)
	{
		const FString Label = FString::Printf(TEXT("[Dir]  %s"), *FPaths::GetCleanFilename(Dir));
		ListContainer->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.HAlign(HAlign_Left)
			.OnClicked(this, &STerraDyneFilePickerPanel::OnEntryClicked, Dir, true)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
		];
	}

	for (const FString& File : Files)
	{
		ListContainer->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.HAlign(HAlign_Left)
			.OnClicked(this, &STerraDyneFilePickerPanel::OnEntryClicked, File, false)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FPaths::GetCleanFilename(File)))
				.ColorAndOpacity(this, &STerraDyneFilePickerPanel::GetEntryColor, File)
			]
		];
	}

	if (Subdirectories.Num() == 0 && Files.Num() == 0)
	{
		ListContainer->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("Empty", "(no subdirectories or .tif/.tiff files here)"))
		];
	}
}

FReply STerraDyneFilePickerPanel::OnUpClicked()
{
	NavigateToDirectory(FPaths::GetPath(CurrentDirectory));
	return FReply::Handled();
}

FReply STerraDyneFilePickerPanel::OnEntryClicked(FString FullPath, bool bIsDirectory)
{
	if (bIsDirectory)
	{
		NavigateToDirectory(FullPath);
	}
	else
	{
		SelectedFilePath = FullPath;
	}
	return FReply::Handled();
}

FReply STerraDyneFilePickerPanel::OnImportClicked()
{
	if (!SelectedFilePath.IsEmpty())
	{
		OnFileChosenDelegate.ExecuteIfBound(SelectedFilePath);
	}
	return FReply::Handled();
}

FReply STerraDyneFilePickerPanel::OnCancelClicked()
{
	OnCancelledDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FText STerraDyneFilePickerPanel::GetCurrentDirectoryText() const
{
	return FText::FromString(CurrentDirectory);
}

FText STerraDyneFilePickerPanel::GetSelectedFileText() const
{
	return SelectedFilePath.IsEmpty()
		? LOCTEXT("NoSelection", "No file selected")
		: FText::FromString(FString::Printf(TEXT("Selected: %s"), *FPaths::GetCleanFilename(SelectedFilePath)));
}

bool STerraDyneFilePickerPanel::IsImportEnabled() const
{
	return !SelectedFilePath.IsEmpty();
}

FSlateColor STerraDyneFilePickerPanel::GetEntryColor(FString FullPath) const
{
	return (FullPath == SelectedFilePath) ? FSlateColor(FLinearColor(0.3f, 0.7f, 1.0f)) : FSlateColor::UseForeground();
}

#undef LOCTEXT_NAMESPACE
