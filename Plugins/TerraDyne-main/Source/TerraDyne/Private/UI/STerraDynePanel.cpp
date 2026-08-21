// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDynePanel.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "World/TerraDyneOrchestrator.h"
#include "IO/TerraDyneArchiveHelper.h"
#include "IO/TerraDyneRasterLayers.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#endif
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "STerraDyneHudStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "TerraDyneUI"

void STerraDynePanel::Construct(const FArguments& InArgs)
{
	OwnerWidget = InArgs._ToolWidget;

	const FSlateFontInfo MicroFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
	const FSlateFontInfo SectionHeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FSlateFontInfo SubHeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo MonospaceFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);

	// Matches the reference's --hud-panel-solid (a dark neutral charcoal, not near-black) -- the
	// panel used to be almost pure black, which read as a generic debug overlay rather than a
	// deliberately dark HUD surface.
	const FLinearColor BGColor = TerraDyneHud::PanelBG;

	// HUD palette: a compact, state-driven scheme (dim/accent/locked, not a rainbow per row) so a
	// layer's colour tells you what it IS -- required-and-empty, locked, or normal -- rather than
	// which one it happens to be. Approximates this project's Lovable HUD mockup (--hud-text/-dim/
	// -locked/-accent/-cyan in its stylesheet); Slate has no CSS variables, so these are named
	// FLinearColor constants instead of themeable tokens.
	// From the shared HUD palette -- these are LINEAR values, see STerraDyneHudStyle.h for why they
	// look far darker than the sRGB colours they render as.
	const FLinearColor HudText = TerraDyneHud::Text;
	const FLinearColor HudDim = TerraDyneHud::Dim;
	const FLinearColor HudLocked = TerraDyneHud::Locked;
	const FLinearColor HudAccent = TerraDyneHud::Accent;
	const FLinearColor HudCyan = TerraDyneHud::Cyan;
	const FLinearColor HudRowBG(0.06f, 0.06f, 0.08f, 0.85f);
	const FSlateFontInfo StatusFont = MonospaceFont;

	// Panel-wide button look: dark control fill, no rounded-corner chrome, tight padding -- the
	// single biggest visual difference between Slate's default (large, rounded, light grey) button
	// and the reference's compact HUD control. Built once and shared by every SButton in this panel
	// via .ButtonStyle(&HudButtonStyle) rather than per-button styling, so the whole panel reads as
	// one consistent control language instead of Slate defaults with a few restyled exceptions.
	TerraDyneHud::MakeButtonStyle(HudButtonStyle);
	TerraDyneHud::MakeBadgeButtonStyle(BadgeButtonStyle);

	auto MakeLayerRow = [this, &SubHeaderFont, &StatusFont, HudText, HudDim, HudLocked, HudAccent]
		(ETerraDyneRasterLayer Layer, const FText& LayerTitle, bool bRequired) -> TSharedRef<SWidget>
	{
		auto TitleColor = [this, Layer, bRequired, HudLocked, HudAccent, HudText]() -> FSlateColor
		{
			if (!IsImportLayerEnabled(Layer) && !IsUnloadLayerEnabled(Layer))
			{
				return HudLocked;
			}
			if (bRequired)
			{
				return HudAccent;
			}
			return HudText;
		};

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LayerTitle)
					.Font(SubHeaderFont)
					.ColorAndOpacity_Lambda(TitleColor)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text_Lambda([this, Layer]()
					{
						return IsUnloadLayerEnabled(Layer) ? LOCTEXT("BtnReplace", "Replace…") : LOCTEXT("BtnImport", "Import…");
					})
					.IsEnabled_Lambda([this, Layer]() { return IsImportLayerEnabled(Layer); })
					.OnClicked(this, &STerraDynePanel::OnImportRasterLayerClicked, Layer)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text(FText::FromString(TEXT("✕")))
					.ToolTipText(LOCTEXT("BtnUnloadTip", "Clear / unload this layer"))
					.IsEnabled_Lambda([this, Layer]() { return IsUnloadLayerEnabled(Layer); })
					.OnClicked(this, &STerraDynePanel::OnUnloadRasterLayerClicked, Layer)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this, Layer]() { return GetLayerStatusText(Layer); })
				.ColorAndOpacity_Lambda([this, Layer]() { return GetLayerStatusColor(Layer); })
				.Font(StatusFont)
				.AutoWrapText(true)
			];
	};

	// 1. Content for LEVELS section
	TSharedRef<SWidget> LevelsContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnSaveFile", "Save to File"))
				.OnClicked(this, &STerraDynePanel::SaveToFile)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnLoadFile", "Load from File"))
				.OnClicked(this, &STerraDynePanel::LoadFromFile)
				.HAlign(HAlign_Center)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetExportStatusText(); })
			.Font(LabelFont)
			.ColorAndOpacity(HudDim)
		];

	// 2. Content for GEOTIFF / RASTER LAYERS section
	TSharedRef<SWidget> GeoTIFFContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::Elevation, LOCTEXT("LayerElevation", "1. Elevation (DEM) • [Required]"), true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::Hillshade, LOCTEXT("LayerHillshade", "2. Hillshade"), false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::ColorRelief, LOCTEXT("LayerColorRelief", "3. Color Relief"), false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::ColorHillshade, LOCTEXT("LayerColorHillshade", "4. Color Hillshade"), false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::Slope, LOCTEXT("LayerSlope", "5. Slope"), false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::Aspect, LOCTEXT("LayerAspect", "6. Aspect"), false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			MakeLayerRow(ETerraDyneRasterLayer::Roughness, LOCTEXT("LayerRoughness", "7. Roughness"), false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DEMRequiredFooter", "Elevation (DEM) is required first as reference grid."))
			.Font(LabelFont)
			.ColorAndOpacity(HudDim)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.2f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnLoadScene", "Load into Scene"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return IsLoadIntoSceneEnabled(); })
				.OnClicked(this, &STerraDynePanel::OnLoadRasterLayersIntoSceneClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(0.8f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnUnloadAll", "Unload All"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return IsUnloadAllEnabled(); })
				.OnClicked(this, &STerraDynePanel::OnUnloadAllRasterLayersClicked)
			]
		];

	// 3. Content for SCULPTING section
	TSharedRef<SWidget> SculptContent = SNew(SVerticalBox)
		// Row 1: Raise / Lower / Smooth
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeRaise", "Raise"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptRaise); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptRaise)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeLower", "Lower"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptLower); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptLower)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeSmooth", "Smooth"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Smooth); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Smooth)
				.HAlign(HAlign_Center)
			]
		]
		// Row 2: Flatten
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.Text(LOCTEXT("ModeFlatten", "Flatten"))
			.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Flatten); })
			.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Flatten)
			.HAlign(HAlign_Center)
		]
		// Dynamic Layers micro-label
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("LayerHeader", "DYNAMIC LAYERS")).Font(MicroFont).ColorAndOpacity(HudDim)
		]
		// Dynamic Layers Row
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("LayerBase", "Base"))
				.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Base); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Base)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("LayerSculpt", "Sculpt"))
				.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Sculpt); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Sculpt)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("LayerDetail", "Detail"))
				.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Detail); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Detail)
				.HAlign(HAlign_Center)
			]
		]
		// Reset Active Layer
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.Text(LOCTEXT("BtnResetLayer", "Reset Active Layer"))
			.ToolTipText(LOCTEXT("TipResetLayer", "Clear all edits on the currently active layer."))
			.OnClicked(this, &STerraDynePanel::ResetActiveLayer)
			.HAlign(HAlign_Center)
		]
		// RADIUS Slider
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("BrushRadius", "RADIUS")).Font(MicroFont).ColorAndOpacity(HudDim)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &STerraDynePanel::GetBrushRadiusText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &STerraDynePanel::GetBrushRadius)
			.OnValueChanged(this, &STerraDynePanel::OnBrushRadiusChanged)
		]
		// STRENGTH Slider
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("BrushStrength", "STRENGTH")).Font(MicroFont).ColorAndOpacity(HudDim)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &STerraDynePanel::GetBrushStrengthText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &STerraDynePanel::GetBrushStrength)
			.OnValueChanged(this, &STerraDynePanel::OnBrushStrengthChanged)
		];

	// 4. Content for ROADS section
	TSharedRef<SWidget> RoadsContent = SNew(SVerticalBox)
		// Draw / Select
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeDrawRoad", "Draw"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::DrawPavedRoad); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::DrawPavedRoad)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeSelectRoad", "Select"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SelectRoad); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SelectRoad)
				.HAlign(HAlign_Center)
			]
		]
		// ROAD WIDTH Slider
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("RoadWidthLabel", "ROAD WIDTH")).Font(MicroFont).ColorAndOpacity(HudDim)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &STerraDynePanel::GetRoadWidthText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &STerraDynePanel::GetRoadWidth)
			.OnValueChanged(this, &STerraDynePanel::OnRoadWidthChanged)
		]
		// ROAD SPLINE
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("RoadSplineHeader", "ROAD SPLINE")).Font(MicroFont).ColorAndOpacity(HudDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRoadAddPoint", "+ Point"))
				.OnClicked(this, &STerraDynePanel::OnAddRoadPointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRoadRemovePoint", "– Point"))
				.IsEnabled_Lambda([this](){ return RoadPointCount > 0; })
				.OnClicked(this, &STerraDynePanel::OnRemoveRoadPointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRoadReset", "Reset"))
				.IsEnabled_Lambda([this](){ return RoadPointCount > 0; })
				.OnClicked(this, &STerraDynePanel::OnResetRoadPointsClicked)
				.HAlign(HAlign_Center)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(this, &STerraDynePanel::GetRoadPointsText)
			.Font(LabelFont)
			.ColorAndOpacity(HudDim)
		];

	// 5. Content for BUILDINGS section
	TSharedRef<SWidget> BuildingsContent = SNew(SVerticalBox)
		// Place / Select
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModePlaceBuilding", "Place"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::PlaceBuilding); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::PlaceBuilding)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeSelectBuilding", "Select"))
				.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SelectBuilding); })
				.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SelectBuilding)
				.HAlign(HAlign_Center)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoBuildingSelectedText", "No building selected"))
			.Font(LabelFont)
			.ColorAndOpacity(HudDim)
		]
		// Stepper: Floors
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const int32 Floors = GetSelectedBuilding()
						? GetSelectedBuilding()->FloorCount
						: (OwnerWidget.IsValid() ? OwnerWidget->PendingFloorCount : 2);
					return FText::Format(LOCTEXT("FloorsFmt", "Floors: {0}"), FText::AsNumber(Floors));
				})
				.Font(LabelFont)
				.ColorAndOpacity(HudText)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnFloorMinus", " – "))
				.OnClicked(this, &STerraDynePanel::OnRemoveFloorClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnFloorPlus", " + "))
				.OnClicked(this, &STerraDynePanel::OnAddFloorClicked)
				.HAlign(HAlign_Center)
			]
		]
		// Footprint Spline
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("FootprintHeader", "FOOTPRINT SPLINE")).Font(MicroFont).ColorAndOpacity(HudDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnAddSplinePoint", "+ Point"))
				.OnClicked(this, &STerraDynePanel::OnAddBuildingPointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRemoveSplinePoint", "– Point"))
				.IsEnabled_Lambda([this](){ return BuildingPointCount > 0; })
				.OnClicked(this, &STerraDynePanel::OnRemoveBuildingPointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnResetFootprint", "Reset"))
				.IsEnabled_Lambda([this](){ return BuildingPointCount > 0; })
				.OnClicked(this, &STerraDynePanel::OnResetBuildingPointsClicked)
				.HAlign(HAlign_Center)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(this, &STerraDynePanel::GetBuildingPointsText)
			.Font(LabelFont)
			.ColorAndOpacity(HudDim)
		]
		// Building index indicator
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(32.0f)
				.HeightOverride(24.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(TerraDyneHud::Control)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BuildingIndex", "1"))
						.Font(SubHeaderFont)
						.ColorAndOpacity(HudText)
					]
				]
			]
		];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>(this, &STerraDynePanel::GetWindowPadding))
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Left)
		[
			SAssignNew(PanelBox, SBox)
			.WidthOverride(320.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BGColor)
				.Padding(6.0f)
				[
					SNew(SVerticalBox)

					//--- HUD HEADER (Draggable) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4, 4, 4, 6)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("HUDMicroTitle", "TERRAIN TOOL"))
									.Font(MicroFont)
									.ColorAndOpacity(HudDim)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("HUDTitle", "RUNTIME HUD"))
									.Font(TitleFont)
									.ColorAndOpacity(HudText)
								]
							]
							// Mode Badge Button
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
							[
								SNew(SButton)
								.ButtonStyle(&BadgeButtonStyle)
								.ToolTipText(LOCTEXT("ModeBadgeTip", "Click to switch session mode"))
								.OnClicked(this, &STerraDynePanel::OnHeaderModeBadgeClicked)
								[
									SNew(STextBlock)
									.Text(this, &STerraDynePanel::GetHeaderModeBadgeText)
									.Font(MicroFont)
									.ColorAndOpacity(HudCyan)
								]
							]
						]
						// Hairline separator underneath header
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
						[
							SNew(SBox).HeightOverride(1.0f)
							[
								SNew(SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
								.BorderBackgroundColor(TerraDyneHud::Line)
								.Padding(0.0f)
							]
						]
					]

					//--- SCROLLABLE COLLAPSIBLE ACCORDION BODY ---
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SScrollBox)
						.Orientation(Orient_Vertical)
						.ScrollBarAlwaysVisible(false)

						// 1. LEVELS SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecLevels", "LEVELS"), bSectionLevelsOpen, LevelsContent, SectionHeaderFont)
						]

						// 2. GEOTIFF / RASTER LAYERS SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							SNew(SBox)
							.Visibility(this, &STerraDynePanel::GetGeoTIFFSectionVisibility)
							[
								MakeCollapsibleSection(LOCTEXT("SecGeoTIFF", "GEOTIFF / RASTER LAYERS"), bSectionGeoTIFFOpen, GeoTIFFContent, SectionHeaderFont)
							]
						]

						// 3. SCULPTING SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecSculpting", "SCULPTING"), bSectionSculptOpen, SculptContent, SectionHeaderFont)
						]

						// 4. ROADS SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecRoads", "ROADS"), bSectionRoadsOpen, RoadsContent, SectionHeaderFont)
						]

						// 5. BUILDINGS SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecBuildings", "BUILDINGS"), bSectionBuildingsOpen, BuildingsContent, SectionHeaderFont)
						]
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> STerraDynePanel::MakeCollapsibleSection(
	const FText& SectionTitle,
	bool& bSectionOpen,
	TSharedRef<SWidget> SectionContent,
	const FSlateFontInfo& SectionHeaderFont)
{
	bool* OpenPtr = &bSectionOpen;

	// Matches the reference's `border-b border-hud-line` under each accordion header -- a hairline
	// rather than a filled panel, so sections read as one continuous list divided by thin rules
	// instead of stacked boxes.
	const FLinearColor HeaderBottomLine(1.0f, 1.0f, 1.0f, 0.08f);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(4.0f, 7.0f))
			.OnClicked_Lambda([OpenPtr]()
			{
				*OpenPtr = !(*OpenPtr);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(SectionTitle)
						.Font(SectionHeaderFont)
						.ColorAndOpacity(FLinearColor(0.90f, 0.90f, 0.92f))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([OpenPtr]()
						{
							return *OpenPtr ? FText::FromString(TEXT("^")) : FText::FromString(TEXT("v"));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(TerraDyneHud::Dim)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[
					SNew(SBox).HeightOverride(1.0f)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(HeaderBottomLine)
						.Padding(0.0f)
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([OpenPtr]()
			{
				return *OpenPtr ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.Padding(FMargin(4.0f, 6.0f, 4.0f, 8.0f))
			[
				SectionContent
			]
		];
}

FReply STerraDynePanel::OnHeaderModeBadgeClicked()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ShowWorldModeChoiceIfNeeded();
	}
	return FReply::Handled();
}

FText STerraDynePanel::GetHeaderModeBadgeText() const
{
	if (const ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->WorldOriginKind == ETerraDyneChunkOrigin::ImportedGeoTIFF || Mgr->HasElevationLayer())
		{
			return LOCTEXT("BadgeDEM", "DEM");
		}
	}
	return LOCTEXT("BadgeSandbox", "SANDBOX");
}

FSlateColor STerraDynePanel::GetHeaderModeBadgeColor() const
{
	if (const ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->WorldOriginKind == ETerraDyneChunkOrigin::ImportedGeoTIFF || Mgr->HasElevationLayer())
		{
			return TerraDyneHud::Cyan;
		}
	}
	return FLinearColor(0.2f, 0.55f, 0.95f, 1.0f);
}

EVisibility STerraDynePanel::GetGeoTIFFSectionVisibility() const
{
	// Same condition the header badge uses for "DEM" vs "SANDBOX" -- this was a stub that always
	// returned Visible, so the raster-layer UI showed up even in a pure Sandbox session with nothing
	// imported and nothing to import into. Only relevant once the world is actually DEM-based (or a
	// layer is already imported, so a Sandbox->DEM switch mid-session doesn't hide in-progress work).
	if (const ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->WorldOriginKind == ETerraDyneChunkOrigin::ImportedGeoTIFF || Mgr->HasElevationLayer())
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

void STerraDynePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

FReply STerraDynePanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Simple hit test: if within top 50px relative to window position
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D RelativeMouse = LocalMouse - WindowPosition;

		if (RelativeMouse.X >= 0 && RelativeMouse.X <= 300 && RelativeMouse.Y >= 0 && RelativeMouse.Y <= 50)
		{
			bIsDragging = true;
			DragOffset = RelativeMouse;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// Absorb anything else that lands on the panel. This handler only runs when no child
		// widget took the click, i.e. the gaps between buttons and the border around them —
		// returning Unhandled there let the click fall straight through to the world, so clicking
		// panel background sculpted the terrain behind it, and with the building tools armed it
		// would drop a building behind the panel.
		if (PanelBox.IsValid() &&
			PanelBox->GetTickSpaceGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
		{
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D NewPos = LocalMouse - DragOffset;

		// Clamp so the panel stays within the viewport
		FVector2D ViewSize = MyGeometry.GetLocalSize();
		NewPos.X = FMath::Clamp(NewPos.X, 0.0f, FMath::Max(0.0f, ViewSize.X - 300.0f));
		NewPos.Y = FMath::Clamp(NewPos.Y, 0.0f, FMath::Max(0.0f, ViewSize.Y - 50.0f));
		WindowPosition = NewPos;

		return FReply::Handled();
	}
	return FReply::Unhandled();
}

//--- Helpers ---
ATerraDyneOrchestrator* STerraDynePanel::GetOrchestrator() const
{
	if (OwnerWidget.IsValid())
	{
		return Cast<ATerraDyneOrchestrator>(UGameplayStatics::GetActorOfClass(OwnerWidget->GetWorld(), ATerraDyneOrchestrator::StaticClass()));
	}
	return nullptr;
}

//--- Buildings ---

ATerraDyneBuildingActor* STerraDynePanel::GetSelectedBuilding() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->SelectedBuilding.Get() : nullptr;
}

bool STerraDynePanel::IsBuildingSelected() const
{
	return GetSelectedBuilding() != nullptr;
}

FText STerraDynePanel::GetSelectedBuildingText() const
{
	if (const ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		return FText::FromString(FString::Printf(
			TEXT("Selected: %s  (%.0f m tall)"),
			*Building->GetName(),
			Building->GetBuildingHeight() / 100.0f));
	}

	return LOCTEXT("NoBuildingSelected", "No building selected");
}

FText STerraDynePanel::GetFloorCountText() const
{
	// Falls back to the pending count so the counter still reads sensibly with nothing selected —
	// that value is what the next placed building will get.
	const int32 Floors = GetSelectedBuilding()
		? GetSelectedBuilding()->FloorCount
		: (OwnerWidget.IsValid() ? OwnerWidget->PendingFloorCount : 0);

	return FText::FromString(FString::Printf(TEXT("%d"), Floors));
}

FReply STerraDynePanel::OnAddFloorClicked()
{
	if (!OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	OwnerWidget->PendingFloorCount = FMath::Clamp(OwnerWidget->PendingFloorCount + 1, 1, 50);
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestSetBuildingFloorCount(Building, OwnerWidget->PendingFloorCount);
		}
		else
		{
			Building->SetFloorCount(OwnerWidget->PendingFloorCount);
		}
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnRemoveFloorClicked()
{
	if (!OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	OwnerWidget->PendingFloorCount = FMath::Clamp(OwnerWidget->PendingFloorCount - 1, 1, 50);
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestSetBuildingFloorCount(Building, OwnerWidget->PendingFloorCount);
		}
		else
		{
			Building->SetFloorCount(OwnerWidget->PendingFloorCount);
		}
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnAddSplinePointClicked()
{
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->RequestAddSplinePoint(Building);
			}
			else
			{
				Building->AddSplinePoint();
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnRemoveSplinePointClicked()
{
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->RequestRemoveSplinePoint(Building);
			}
			else
			{
				Building->RemoveSplinePoint();
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnResetFootprintClicked()
{
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->RequestResetFootprint(Building);
			}
			else
			{
				Building->ResetToDefaultFootprint();
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnRelevelClicked()
{
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->RequestRelevelBuilding(Building);
			}
			else
			{
				Building->RelevelToTerrain();
				Building->SnapToPadHeight();
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnDeleteBuildingClicked()
{
	ATerraDyneBuildingActor* Building = GetSelectedBuilding();
	if (!Building)
	{
		return FReply::Unhandled();
	}

	// Clear the selection first: Destroy() runs EndPlay, which unregisters the building and
	// rebuilds the terrain pads, and the panel must not be holding it while that happens.
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->SelectedBuilding = nullptr;
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestDeleteBuilding(Building);
		}
		else
		{
			Building->Destroy();
		}
	}
	else
	{
		Building->Destroy();
	}

	return FReply::Handled();
}

ATerraDyneManager* STerraDynePanel::GetManager() const
{
	if (OwnerWidget.IsValid())
	{
		if (UWorld* World = OwnerWidget->GetWorld())
		{
			// Assuming Manager is a singleton or accessible via Subsystem, but looking for Actor for now
			return Cast<ATerraDyneManager>(UGameplayStatics::GetActorOfClass(World, ATerraDyneManager::StaticClass()));
		}
	}
	return nullptr;
}

//--- Callbacks ---

FText STerraDynePanel::GetBrushRadiusText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("RadiusFmt", "{0}m"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushRadius / 100.f)));
	}
	return LOCTEXT("RadiusNA", "--");
}

FText STerraDynePanel::GetBrushStrengthText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("StrengthFmt", "{0}%"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushStrength * 20.0f)));
	}
	return LOCTEXT("StrengthNA", "--");
}

FReply STerraDynePanel::StartPersistenceTest()
{
	if (ATerraDyneOrchestrator* Orch = GetOrchestrator())
	{
		Orch->StartPersistenceTest();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::SaveToFile()
{
	const FString DefaultFolder = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerraDyneExports")));
	IFileManager::Get().MakeDirectory(*DefaultFolder, true);

	FString TargetPath = FPaths::Combine(DefaultFolder, TEXT("MyLevel.terradyne"));

#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Save TerraDyne Level"),
			DefaultFolder,
			TEXT("MyLevel.terradyne"),
			TEXT("TerraDyne Level (*.terradyne)|*.terradyne"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			TargetPath = OutFiles[0];
		}
		else
		{
			return FReply::Handled();
		}
	}
#endif

	LastExportPath = TargetPath;
	if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestSaveWorldToFile(TargetPath);
			return FReply::Handled();
		}
	}

	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->SaveWorldToFile(TargetPath);
	}
	return FReply::Handled();
}

FReply STerraDynePanel::LoadFromFile()
{
	const FString DefaultFolder = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerraDyneExports")));

	FString TargetPath = FPaths::Combine(DefaultFolder, TEXT("MyLevel.terradyne"));

#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Load TerraDyne Level"),
			DefaultFolder,
			TEXT(""),
			TEXT("TerraDyne Level (*.terradyne)|*.terradyne"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			TargetPath = OutFiles[0];
		}
		else
		{
			return FReply::Handled();
		}
	}
#endif

	LastExportPath = TargetPath;
	if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestLoadWorldFromFile(TargetPath);
			return FReply::Handled();
		}
	}

	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->LoadWorldFromFile(TargetPath);
	}
	return FReply::Handled();
}

FText STerraDynePanel::GetExportStatusText() const
{
	return LastExportPath.IsEmpty()
		? LOCTEXT("ExportNone", "No file selected yet")
		: FText::Format(LOCTEXT("ExportLast", "Last: {0}"), FText::FromString(LastExportPath));
}

FReply STerraDynePanel::OnImportGeoTIFFClicked()
{
	PickAndImportFileForLayer(ETerraDyneRasterLayer::Elevation);
	return FReply::Handled();
}

FReply STerraDynePanel::OnImportRasterLayerClicked(ETerraDyneRasterLayer Layer)
{
	PickAndImportFileForLayer(Layer);
	return FReply::Handled();
}

FReply STerraDynePanel::OnUnloadRasterLayerClicked(ETerraDyneRasterLayer Layer)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->UnloadRasterLayer(Layer);
		LastRasterStatusMessage = FString::Printf(TEXT("Unloaded %s."), *TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
		bLastRasterStatusIsError = false;
	}
	else if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestUnloadRasterLayer(Layer);
		}
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnUnloadAllRasterLayersClicked()
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->UnloadAllRasterLayers();
		LastRasterStatusMessage = TEXT("All raster layers unloaded and terrain cleared.");
		bLastRasterStatusIsError = false;
	}
	else if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestUnloadAllRasterLayers();
		}
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnLoadRasterLayersIntoSceneClicked()
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		FString OutError;
		if (Mgr->LoadRasterLayersIntoScene(OutError))
		{
			LastRasterStatusMessage = TEXT("Terrain loaded successfully into scene.");
			bLastRasterStatusIsError = false;
		}
		else
		{
			LastRasterStatusMessage = OutError;
			bLastRasterStatusIsError = true;
		}
	}
	else if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestLoadRasterLayersIntoScene();
		}
	}
	return FReply::Handled();
}

bool STerraDynePanel::IsImportLayerEnabled(ETerraDyneRasterLayer Layer) const
{
	if (Layer == ETerraDyneRasterLayer::Elevation)
	{
		return true;
	}

	if (ATerraDyneManager* Mgr = GetManager())
	{
		return Mgr->HasElevationLayer();
	}
	return false;
}

bool STerraDynePanel::IsUnloadLayerEnabled(ETerraDyneRasterLayer Layer) const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		const int32 Index = (int32)Layer;
		return Mgr->RasterLayerSlots.IsValidIndex(Index) && Mgr->RasterLayerSlots[Index].bImported;
	}
	return false;
}

bool STerraDynePanel::IsLoadIntoSceneEnabled() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		return Mgr->HasElevationLayer();
	}
	return false;
}

bool STerraDynePanel::IsUnloadAllEnabled() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		for (const FTerraDyneRasterLayerSlot& Slot : Mgr->RasterLayerSlots)
		{
			if (Slot.bImported)
			{
				return true;
			}
		}
	}
	return false;
}

FText STerraDynePanel::GetLayerStatusText(ETerraDyneRasterLayer Layer) const
{
	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr)
	{
		return LOCTEXT("NoManager", "Manager unavailable");
	}

	const int32 Index = (int32)Layer;
	if (!Mgr->RasterLayerSlots.IsValidIndex(Index))
	{
		return LOCTEXT("InvalidSlot", "Invalid Slot");
	}

	const FTerraDyneRasterLayerSlot& Slot = Mgr->RasterLayerSlots[Index];
	if (Slot.bImported)
	{
		const FString LoadedState = Slot.bLoadedIntoScene ? TEXT(" [In Scene]") : TEXT("");
		return FText::FromString(FString::Printf(TEXT("%s (%dx%d, %d band%s)%s"),
			*FPaths::GetCleanFilename(Slot.SourceFilePath),
			Slot.Width, Slot.Height,
			Slot.BandCount,
			Slot.BandCount > 1 ? TEXT("s") : TEXT(""),
			*LoadedState));
	}

	if (Layer == ETerraDyneRasterLayer::Elevation)
	{
		return LOCTEXT("ElevationRequired", "Required - Import first as base grid");
	}

	if (!Mgr->HasElevationLayer())
	{
		return LOCTEXT("OverlayLocked", "Locked - Import Elevation DEM first");
	}

	return LOCTEXT("OverlayEmpty", "Not imported (optional)");
}

FSlateColor STerraDynePanel::GetLayerStatusColor(ETerraDyneRasterLayer Layer) const
{
	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr)
	{
		return FLinearColor::Gray;
	}

	const int32 Index = (int32)Layer;
	if (Mgr->RasterLayerSlots.IsValidIndex(Index) && Mgr->RasterLayerSlots[Index].bImported)
	{
		return FLinearColor(0.2f, 0.85f, 0.3f); // Green
	}

	if (Layer == ETerraDyneRasterLayer::Elevation)
	{
		return FLinearColor(1.0f, 0.75f, 0.2f); // Amber / Warning
	}

	if (!Mgr->HasElevationLayer())
	{
		return FLinearColor(0.45f, 0.45f, 0.45f); // Dim grey
	}

	return FLinearColor(0.65f, 0.65f, 0.65f); // Light grey
}

FText STerraDynePanel::GetOverallRasterStatusText() const
{
	if (!LastRasterStatusMessage.IsEmpty())
	{
		return FText::FromString(LastRasterStatusMessage);
	}

	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->HasElevationLayer())
		{
			int32 ImportedCount = 0;
			for (const FTerraDyneRasterLayerSlot& Slot : Mgr->RasterLayerSlots)
			{
				if (Slot.bImported)
				{
					++ImportedCount;
				}
			}
			return FText::Format(LOCTEXT("RasterCountStatus", "{0} of 7 layers imported and verified compatible."), FText::AsNumber(ImportedCount));
		}
	}

	return LOCTEXT("RasterInitialStatus", "Elevation (DEM) is required first as reference grid.");
}

FSlateColor STerraDynePanel::GetOverallRasterStatusColor() const
{
	if (bLastRasterStatusIsError)
	{
		return FLinearColor(1.0f, 0.35f, 0.35f); // Red
	}
	if (!LastRasterStatusMessage.IsEmpty())
	{
		return FLinearColor(0.3f, 0.9f, 0.4f); // Green
	}
	return FLinearColor(0.7f, 0.7f, 0.7f); // Subtle grey
}

void STerraDynePanel::PickAndImportFileForLayer(ETerraDyneRasterLayer Layer)
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const FString Title = FString::Printf(TEXT("Import %s"), *TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			Title,
			TEXT(""),
			TEXT(""),
			TEXT("GeoTIFF & Raster Archives (*.tif;*.tiff;*.tar.gz;*.tgz;*.zip)|*.tif;*.tiff;*.tar.gz;*.tgz;*.zip|GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|Archives (*.tar.gz;*.tgz;*.zip)|*.tar.gz;*.tgz;*.zip|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			FString ResolvedPath;
			FString ExtractError;
			if (!FTerraDyneArchiveHelper::ResolveOrExtractRaster(OutFiles[0], Layer, ResolvedPath, ExtractError))
			{
				LastRasterStatusMessage = FString::Printf(TEXT("Failed to extract: %s"), *ExtractError);
				bLastRasterStatusIsError = true;
				return;
			}

			if (ATerraDyneManager* Mgr = GetManager())
			{
				FString OutError;
				if (Mgr->ImportRasterLayer(Layer, ResolvedPath, OutError))
				{
					LastRasterStatusMessage = FString::Printf(TEXT("Successfully imported %s: %s"),
						*TerraDyneRasterLayers::GetDisplayName(Layer).ToString(),
						*FPaths::GetCleanFilename(ResolvedPath));
					bLastRasterStatusIsError = false;
				}
				else
				{
					LastRasterStatusMessage = OutError;
					bLastRasterStatusIsError = true;
				}
			}
			else if (OwnerWidget.IsValid())
			{
				if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
				{
					PC->RequestImportRasterLayer(Layer, ResolvedPath);
				}
			}
		}
		return;
	}
#endif

	if (OwnerWidget.IsValid())
	{
		OwnerWidget->OpenRasterLayerImportPicker(Layer);
	}
}

FReply STerraDynePanel::StartShowcase()
{
	ATerraDyneOrchestrator* Orch = GetOrchestrator();
	if (!Orch && OwnerWidget.IsValid() && OwnerWidget->GetWorld())
	{
		Orch = OwnerWidget->GetWorld()->SpawnActor<ATerraDyneOrchestrator>(
			ATerraDyneOrchestrator::StaticClass(),
			FVector(0.0f, 0.0f, 500.0f),
			FRotator::ZeroRotator);
	}

	if (Orch)
	{
		Orch->RunScriptedShowcase();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply STerraDynePanel::SetActiveLayer(ETerraDyneLayer NewLayer)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->ActiveLayer = NewLayer;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetLayerColor(ETerraDyneLayer Layer) const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->ActiveLayer == Layer)
		{
			return TerraDyneHud::ActiveButtonTint;
		}
	}
	return TerraDyneHud::InactiveButtonTint;
}

FReply STerraDynePanel::ResetActiveLayer()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ResetActiveLayer();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::SetToolMode(ETerraDyneToolMode NewMode)
{
	if (OwnerWidget.IsValid())
	{
		// Switching away from a road tool used to leave the previous selection's gizmo/highlight
		// stuck on screen -- nothing but road-specific actions (delete point, load world) ever
		// cleared it. A mode switch is exactly the point a stale selection should go away too.
		if (IsRoadToolMode(OwnerWidget->CurrentTool) && !IsRoadToolMode(NewMode))
		{
			if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->ClearRoadSelection();
			}
		}

		OwnerWidget->CurrentTool = NewMode;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetToolModeColor(ETerraDyneToolMode Mode) const
{
	if (OwnerWidget.IsValid())
	{
		if (OwnerWidget->CurrentTool == Mode)
		{
			return TerraDyneHud::ActiveButtonTint;
		}
	}
	return TerraDyneHud::InactiveButtonTint;
}

float STerraDynePanel::GetBrushRadius() const
{
	return OwnerWidget.IsValid() ? (OwnerWidget->BrushRadius / 10000.0f) : 0.2f;
}

void STerraDynePanel::OnBrushRadiusChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushRadius = FMath::Clamp(NewValue * 10000.0f, 100.0f, 10000.0f);
	}
}

float STerraDynePanel::GetBrushStrength() const
{
	return OwnerWidget.IsValid() ? (OwnerWidget->BrushStrength / 5.0f) : 0.1f;
}

void STerraDynePanel::OnBrushStrengthChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushStrength = FMath::Clamp(NewValue * 5.0f, 0.0f, 5.0f);
	}
}

FReply STerraDynePanel::OnAddRoadPointClicked()
{
	RoadPointCount++;
	return FReply::Handled();
}

FReply STerraDynePanel::OnRemoveRoadPointClicked()
{
	if (RoadPointCount > 0)
	{
		RoadPointCount--;
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnResetRoadPointsClicked()
{
	RoadPointCount = 0;
	return FReply::Handled();
}

float STerraDynePanel::GetRoadWidth() const
{
	return RoadWidth / 50.0f;
}

void STerraDynePanel::OnRoadWidthChanged(float NewValue)
{
	RoadWidth = FMath::Clamp(NewValue * 50.0f, 1.0f, 50.0f);
}

FText STerraDynePanel::GetRoadWidthText() const
{
	return FText::Format(LOCTEXT("RoadWidthFmt", "{0}m"), FText::AsNumber(FMath::RoundToInt(RoadWidth)));
}

FText STerraDynePanel::GetRoadPointsText() const
{
	return FText::Format(LOCTEXT("RoadPointsFmt", "{0} points"), FText::AsNumber(RoadPointCount));
}

FReply STerraDynePanel::OnAddBuildingPointClicked()
{
	BuildingPointCount++;
	return FReply::Handled();
}

FReply STerraDynePanel::OnRemoveBuildingPointClicked()
{
	if (BuildingPointCount > 0)
	{
		BuildingPointCount--;
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnResetBuildingPointsClicked()
{
	BuildingPointCount = 0;
	return FReply::Handled();
}

FText STerraDynePanel::GetBuildingPointsText() const
{
	return FText::Format(LOCTEXT("BuildingPointsFmt", "{0} points"), FText::AsNumber(BuildingPointCount));
}

FText STerraDynePanel::GetStatsText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		return FText::Format(LOCTEXT("StatsFmt", "Chunks: {0} | Verts: {1}"), 
			FText::AsNumber(Mgr->GetActiveChunkCount()), 
			FText::AsNumber(Mgr->GetTotalVertexCount()));
	}
	return LOCTEXT("StatsNA", "Terrain Manager Not Found");
}

FText STerraDynePanel::GetGPUStatsText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		FTerraDyneGPUStats Stats = Mgr->GetGPUStats();
		return FText::Format(LOCTEXT("GPUStatsFmt", "{0}\n{1}"), 
			FText::FromString(Stats.ComputeBackend),
			FText::FromString(Stats.AdapterName));
	}
	return FText::GetEmpty();
}

FSlateColor STerraDynePanel::GetGPUStatusColor() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->GetGPUStats().bIsCudaAvailable)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // Blue for hybrid RT-assisted path
		}
	}
	return FLinearColor::Gray;
}

bool STerraDynePanel::IsUndoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			return Mgr->CanUndo(PC);
		}
	}
	return false;
}

bool STerraDynePanel::IsRedoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			return Mgr->CanRedo(PC);
		}
	}
	return false;
}

FReply STerraDynePanel::OnUndoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	// Straight to the manager rather than through a player-controller cast. This used to require
	// ATerraDyneEditController, which is not the controller the game actually spawns — so the cast
	// always failed and the button silently did nothing.
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		// Road edits first, so one control serves both histories without either swallowing the
		// other's steps.
		if (ATerraDyneEditController* EditPC = Cast<ATerraDyneEditController>(PC))
		{
			if (EditPC->TryRoadUndo())
			{
				return FReply::Handled();
			}
		}

		if (ATerraDyneManager* Mgr = GetManager())
		{
			Mgr->Undo(PC);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnRedoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneEditController* EditPC = Cast<ATerraDyneEditController>(PC))
		{
			if (EditPC->TryRoadRedo())
			{
				return FReply::Handled();
			}
		}

		if (ATerraDyneManager* Mgr = GetManager())
		{
			Mgr->Redo(PC);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::DeleteRoadPoint()
{
	if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->DeleteSelectedRoadPoint();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

bool STerraDynePanel::HasRoadPointSelected() const
{
	if (OwnerWidget.IsValid())
	{
		if (const ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			return PC->HasRoadPointSelected();
		}
	}
	return false;
}

FText STerraDynePanel::GetRoadStatusText() const
{
	if (OwnerWidget.IsValid())
	{
		if (OwnerWidget->CurrentTool == ETerraDyneToolMode::DrawPavedRoad)
		{
			return LOCTEXT("RoadStatusPaved", "Active: Click terrain to draw Paved Road.");
		}
		if (OwnerWidget->CurrentTool == ETerraDyneToolMode::DrawRoughRoad)
		{
			return LOCTEXT("RoadStatusRough", "Active: Click terrain to draw Rough Trail.");
		}
		if (OwnerWidget->CurrentTool == ETerraDyneToolMode::SelectRoad)
		{
			return LOCTEXT("RoadStatusSelect", "Active: Click a road, then drag its points.");
		}
	}
	return LOCTEXT("RoadStatusIdle", "Select a road tool above to begin.");
}

#undef LOCTEXT_NAMESPACE
