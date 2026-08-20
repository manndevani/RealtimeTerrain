// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDyneWorldModeChoicePanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "TerraDyneWorldModeChoice"

void STerraDyneWorldModeChoicePanel::Construct(const FArguments& InArgs)
{
	OnSandboxChosenDelegate = InArgs._OnSandboxChosen;
	OnImportChosenDelegate = InArgs._OnImportChosen;

	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 20);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	const FLinearColor BGColor(0.05f, 0.05f, 0.05f, 0.95f);

	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(420.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BGColor)
				.Padding(20.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Title", "How do you want to start?"))
						.Font(TitleFont)
						.Justification(ETextJustify::Center)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Subtitle", "Nothing spawns until you pick one."))
						.Font(LabelFont)
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(12.0f, 10.0f))
						.OnClicked(this, &STerraDyneWorldModeChoicePanel::OnSandboxClicked)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("SandboxTitle", "Sandbox")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("SandboxDesc", "Procedural terrain, same as today."))
								.Font(LabelFont)
							]
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(12.0f, 10.0f))
						.OnClicked(this, &STerraDyneWorldModeChoicePanel::OnImportClicked)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("ImportTitle", "Import GIS File")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("ImportDesc", "Load real-world terrain from a GeoTIFF DEM."))
								.Font(LabelFont)
							]
						]
					]
				]
			]
		]
	];
}

FReply STerraDyneWorldModeChoicePanel::OnSandboxClicked()
{
	OnSandboxChosenDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply STerraDyneWorldModeChoicePanel::OnImportClicked()
{
	OnImportChosenDelegate.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
