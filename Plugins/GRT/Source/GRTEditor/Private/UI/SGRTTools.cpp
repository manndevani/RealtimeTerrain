// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/SGRTTools.h"
#include "Baking/GRTBaker.h"

// Engine Includes
#include "LandscapeProxy.h"
#include "EngineUtils.h" // For TActorIterator
#include "Editor.h" 
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SGRTTools"

void SGRTTools::Construct(const FArguments& InArgs)
{
	SavePath = TEXT("/Game/GRT/BakedData");

	RefreshLandscapeList();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(10)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			//--- Title ---//
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "GRT Conversion Wizard"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				.ColorAndOpacity(FLinearColor::White)
			]

			//--- Landscape Selector ---//
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SelectLbl", "Source Landscape:"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SAssignNew(LandscapeComboBox, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&LandscapeOptions)
				.OnGenerateWidget(this, &SGRTTools::OnGenerateComboWidget)
				.OnSelectionChanged(this, &SGRTTools::OnLandscapeSelectionChanged)
				.InitiallySelectedItem(SelectedLandscapeOption)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return SelectedLandscapeOption.IsValid() ? FText::FromString(*SelectedLandscapeOption) : LOCTEXT("None", "None Selected");
					})
				]
			]

			//--- Path Input ---//
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PathLbl", "Output Folder (Content Browser Path):"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 20)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(SavePath))
				.OnTextChanged(this, &SGRTTools::OnPathTextChanged)
			]

			//--- Action Button ---//
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ContentPadding(FMargin(10, 5))
				.OnClicked(this, &SGRTTools::OnBakeClicked)
				.IsEnabled_Lambda([this]() { return SelectedLandscapeActor.IsValid(); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeBtn", "Bake to Data Assets"))
					.Font(FAppStyle::GetFontStyle("HeadingSmall"))
				]
			]

			//--- Status Bar ---//
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 10)
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Text(FText::GetEmpty())
				.ColorAndOpacity(FLinearColor::Green)
			]
		]
	];
}

void SGRTTools::RefreshLandscapeList()
{
	LandscapeOptions.Empty();
	SelectedLandscapeActor.Reset();

	// Get the correct Editor World
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	// Iterate all Landscape Actors
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Land = *It;
		if (Land)
		{
			LandscapeOptions.Add(MakeShared<FString>(Land->GetActorLabel()));
		}
	}

	// Default select selection
	if (LandscapeOptions.Num() > 0)
	{
		SelectedLandscapeOption = LandscapeOptions[0];
		// Resolve the actor immediately for the first item
		// (For robust code, lookup via label/name in OnSelectionChanged)
		OnLandscapeSelectionChanged(SelectedLandscapeOption, ESelectInfo::Direct);
	}
	else
	{
		SelectedLandscapeOption.Reset();
	}

	if (LandscapeComboBox.IsValid())
	{
		LandscapeComboBox->RefreshOptions();
	}
}

TSharedRef<SWidget> SGRTTools::OnGenerateComboWidget(TSharedPtr<FString> Item)
{
	return SNew(STextBlock).Text(FText::FromString(*Item));
}

void SGRTTools::OnLandscapeSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	SelectedLandscapeOption = NewValue;
	SelectedLandscapeActor.Reset();

	if (!NewValue.IsValid()) return;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	// Find the actor that matches the name
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (It->GetActorLabel() == *NewValue)
		{
			SelectedLandscapeActor = *It;
			break;
		}
	}
}

void SGRTTools::OnPathTextChanged(const FText& NewText)
{
	SavePath = NewText.ToString();
}

FReply SGRTTools::OnBakeClicked()
{
	if (!SelectedLandscapeActor.IsValid())
	{
		StatusTextBlock->SetText(LOCTEXT("ErrSel", "Error: No Landscape Selected."));
		StatusTextBlock->SetColorAndOpacity(FLinearColor::Red);
		return FReply::Handled();
	}

	StatusTextBlock->SetText(LOCTEXT("Working", "Baking... (Check Output Log)"));
	StatusTextBlock->SetColorAndOpacity(FLinearColor::Yellow);

	// --- CALL THE BAKER ---
	TArray<UGRTTileData*> Assets = UGRTBaker::BakeLandscapeToAssets(SelectedLandscapeActor.Get(), SavePath);

	if (Assets.Num() > 0)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Count"), Assets.Num());
		StatusTextBlock->SetText(FText::Format(LOCTEXT("Success", "Success! Baked {Count} Data Assets."), Args));
		StatusTextBlock->SetColorAndOpacity(FLinearColor::Green);
	}
	else
	{
		StatusTextBlock->SetText(LOCTEXT("Fail", "Baking Failed. See Log."));
		StatusTextBlock->SetColorAndOpacity(FLinearColor::Red);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
