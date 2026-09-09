#include "SGRTFilePickerPanel.h"
#include "IO/GRTArchiveHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Brushes/SlateColorBrush.h"
#include "SGRTHudStyle.h"

#define LOCTEXT_NAMESPACE "GRTFilePicker"

void SGRTFilePickerPanel::Construct(const FArguments& InArgs)
{
	OnFileChosenDelegate = InArgs._OnFileChosen;
	OnCancelledDelegate = InArgs._OnCancelled;
	AllowedExtensions = InArgs._AllowedExtensions;

	const FSlateFontInfo MicroFont = FCoreStyle::GetDefaultFontStyle("Bold", 7);
	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);

	GRTHud::MakeButtonStyle(HudButtonStyle);
	GRTHud::MakeRowStyle(HudEntryStyle);

	// Structure mirrors the reference HUD: a micro eyebrow label over an uppercase title, a hairline
	// outline around the whole panel, and a recessed list body -- rather than one flat grey box.
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(560.0f)
			.HeightOverride(470.0f)
			[
				// Outer 1px hairline: an outline-coloured border padded by 1, wrapping the real
				// panel surface. Slate borders have no stroke of their own, so the outline has to be
				// built from a nested pair like this.
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(GRTHud::LineStrong)
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(GRTHud::PanelBG)
					.Padding(FMargin(14.0f, 12.0f))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Eyebrow", "IMPORT"))
							.Font(MicroFont)
							.ColorAndOpacity(GRTHud::Dim)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 10)
						[
							SNew(STextBlock)
							.Text(InArgs._TitleOverride.IsEmpty()
								? LOCTEXT("Title", "SELECT A GEOTIFF  (.TIF / .TIFF)")
								: InArgs._TitleOverride)
							.Font(HeaderFont)
							.ColorAndOpacity(GRTHud::Text)
						]

						// Current directory + Up button
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
							[
								SNew(SButton)
								.ButtonStyle(&HudButtonStyle)
								.OnClicked(this, &SGRTFilePickerPanel::OnUpClicked)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("Up", "UP"))
									.Font(MicroFont)
									.ColorAndOpacity(GRTHud::Text)
								]
							]
							+ SHorizontalBox::Slot().VAlign(VAlign_Center).FillWidth(1.0f)
							[
								SNew(STextBlock)
								.Text(this, &SGRTFilePickerPanel::GetCurrentDirectoryText)
								.Font(LabelFont)
								.ColorAndOpacity(GRTHud::Dim)
								.AutoWrapText(true)
							]
						]

						// Listing (recessed, hairline-outlined)
						+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 0, 0, 10)
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(GRTHud::Line)
							.Padding(1.0f)
							[
								SNew(SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
								.BorderBackgroundColor(GRTHud::SurfaceBG)
								.Padding(4.0f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(ListContainer, SVerticalBox)
									]
								]
							]
						]

						// Selected file + Import/Cancel
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(this, &SGRTFilePickerPanel::GetSelectedFileText)
							.Font(LabelFont)
							.ColorAndOpacity(this, &SGRTFilePickerPanel::GetSelectedFileColor)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 6, 0)
							[
								SNew(SButton)
								.ButtonStyle(&HudButtonStyle)
								.HAlign(HAlign_Center)
								.IsEnabled(this, &SGRTFilePickerPanel::IsImportEnabled)
								.OnClicked(this, &SGRTFilePickerPanel::OnImportClicked)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("Import", "IMPORT"))
									.Font(MicroFont)
									.ColorAndOpacity(this, &SGRTFilePickerPanel::GetImportLabelColor)
								]
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SButton)
								.ButtonStyle(&HudButtonStyle)
								.HAlign(HAlign_Center)
								.OnClicked(this, &SGRTFilePickerPanel::OnCancelClicked)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("Cancel", "CANCEL"))
									.Font(MicroFont)
									.ColorAndOpacity(GRTHud::Dim)
								]
							]
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

bool SGRTFilePickerPanel::IsAcceptedFile(const FString& Filename) const
{
	if (AllowedExtensions.Num() == 0)
	{
		return FGRTArchiveHelper::HasSupportedExtension(Filename);
	}

	const FString Ext = FPaths::GetExtension(Filename).ToLower();
	return AllowedExtensions.ContainsByPredicate(
		[&Ext](const FString& Allowed) { return Allowed.ToLower() == Ext; });
}

void SGRTFilePickerPanel::NavigateToDirectory(const FString& Path)
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

void SGRTFilePickerPanel::RefreshListing()
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
		else if (IsAcceptedFile(Entry))
		{
			Files.Add(Entry);
		}
		return true;
	});

	Subdirectories.Sort();
	Files.Sort();

	RebuildListWidgets();
}

void SGRTFilePickerPanel::RebuildListWidgets()
{
	if (!ListContainer.IsValid())
	{
		return;
	}

	ListContainer->ClearChildren();

	const FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FLinearColor HudDirColor(0.62f, 0.75f, 0.92f);
	const FLinearColor HudDim(0.56f, 0.57f, 0.62f);

	for (const FString& Dir : Subdirectories)
	{
		const FString Label = FString::Printf(TEXT("[Dir]  %s"), *FPaths::GetCleanFilename(Dir));
		ListContainer->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(&HudEntryStyle)
			.HAlign(HAlign_Left)
			.OnClicked(this, &SGRTFilePickerPanel::OnEntryClicked, Dir, true)
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(RowFont).ColorAndOpacity(HudDirColor)
			]
		];
	}

	for (const FString& File : Files)
	{
		ListContainer->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(&HudEntryStyle)
			.HAlign(HAlign_Left)
			.OnClicked(this, &SGRTFilePickerPanel::OnEntryClicked, File, false)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FPaths::GetCleanFilename(File)))
				.Font(RowFont)
				.ColorAndOpacity(this, &SGRTFilePickerPanel::GetEntryColor, File)
			]
		];
	}

	if (Subdirectories.Num() == 0 && Files.Num() == 0)
	{
		ListContainer->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("Empty", "(nothing here matching this file type)")).Font(RowFont).ColorAndOpacity(HudDim)
		];
	}
}

FReply SGRTFilePickerPanel::OnUpClicked()
{
	NavigateToDirectory(FPaths::GetPath(CurrentDirectory));
	return FReply::Handled();
}

FReply SGRTFilePickerPanel::OnEntryClicked(FString FullPath, bool bIsDirectory)
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

FReply SGRTFilePickerPanel::OnImportClicked()
{
	if (!SelectedFilePath.IsEmpty())
	{
		OnFileChosenDelegate.ExecuteIfBound(SelectedFilePath);
	}
	return FReply::Handled();
}

FReply SGRTFilePickerPanel::OnCancelClicked()
{
	OnCancelledDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FText SGRTFilePickerPanel::GetCurrentDirectoryText() const
{
	return FText::FromString(CurrentDirectory);
}

FText SGRTFilePickerPanel::GetSelectedFileText() const
{
	return SelectedFilePath.IsEmpty()
		? LOCTEXT("NoSelection", "No file selected")
		: FText::FromString(FString::Printf(TEXT("Selected: %s"), *FPaths::GetCleanFilename(SelectedFilePath)));
}

bool SGRTFilePickerPanel::IsImportEnabled() const
{
	return !SelectedFilePath.IsEmpty();
}

FSlateColor SGRTFilePickerPanel::GetEntryColor(FString FullPath) const
{
	return (FullPath == SelectedFilePath)
		? FSlateColor(GRTHud::Accent)
		: FSlateColor(GRTHud::Text);
}

FSlateColor SGRTFilePickerPanel::GetSelectedFileColor() const
{
	// Dim while nothing is chosen ("No file selected" is a prompt, not a result), accent once it is.
	return SelectedFilePath.IsEmpty() ? FSlateColor(GRTHud::Dim) : FSlateColor(GRTHud::Accent);
}

FSlateColor SGRTFilePickerPanel::GetImportLabelColor() const
{
	// Slate does not tint button content by enabled-state, so the label has to carry it itself --
	// otherwise a disabled Import still reads as a live, clickable control.
	return IsImportEnabled() ? FSlateColor(GRTHud::Accent) : FSlateColor(GRTHud::Locked);
}

#undef LOCTEXT_NAMESPACE
