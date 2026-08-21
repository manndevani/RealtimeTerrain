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

	const FSlateFontInfo MicroFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 20);
	const FSlateFontInfo CardTitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo ButtonFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	const FLinearColor BGColor(0.04f, 0.04f, 0.05f, 0.95f);
	const FLinearColor CardBGColor(0.08f, 0.08f, 0.1f, 0.9f);
	const FLinearColor AccentColor(0.2f, 0.6f, 1.0f, 1.0f);

	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(640.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BGColor)
				.Padding(24.0f)
				[
					SNew(SVerticalBox)

					// Micro-header
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MicroHeader", "REALTIME TERRAIN EDITOR"))
						.Font(MicroFont)
						.ColorAndOpacity(FLinearColor(0.5f, 0.55f, 0.65f))
					]

					// Main Title
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MainTitle", "Select session mode"))
						.Font(TitleFont)
						.ColorAndOpacity(FLinearColor::White)
					]

					// 2-Card Horizontal Row
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)

						// CARD 1: SANDBOX
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(CardBGColor)
							.Padding(16.0f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CardSandboxTitle", "⛰️ SANDBOX"))
									.Font(CardTitleFont)
									.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.95f))
								]
								+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 0, 0, 16)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CardSandboxDesc", "Flat procedural grid — jump straight into sculpting, roads and buildings."))
									.Font(BodyFont)
									.ColorAndOpacity(FLinearColor(0.6f, 0.65f, 0.7f))
									.AutoWrapText(true)
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SButton)
									.HAlign(HAlign_Center)
									.ContentPadding(FMargin(10.0f, 8.0f))
									.ButtonColorAndOpacity(AccentColor)
									.OnClicked(this, &STerraDyneWorldModeChoicePanel::OnSandboxClicked)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("BtnEnterSandbox", "ENTER SANDBOX"))
										.Font(ButtonFont)
										.ColorAndOpacity(FLinearColor::White)
									]
								]
							]
						]

						// CARD 2: IMPORT DEM DATA
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8, 0, 0, 0)
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(CardBGColor)
							.Padding(16.0f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CardImportTitle", "📑 IMPORT DEM DATA"))
									.Font(CardTitleFont)
									.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.2f))
								]
								+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 0, 0, 16)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CardImportDesc", "Fill the GeoTIFF raster stack first; elevation is required before the scene loads."))
									.Font(BodyFont)
									.ColorAndOpacity(FLinearColor(0.6f, 0.65f, 0.7f))
									.AutoWrapText(true)
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SButton)
									.HAlign(HAlign_Center)
									.ContentPadding(FMargin(10.0f, 8.0f))
									.ButtonColorAndOpacity(FLinearColor(0.85f, 0.65f, 0.15f, 1.0f))
									.OnClicked(this, &STerraDyneWorldModeChoicePanel::OnImportClicked)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("BtnEnterImport", "ENTER DEM IMPORT"))
										.Font(ButtonFont)
										.ColorAndOpacity(FLinearColor::Black)
									]
								]
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
