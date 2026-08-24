// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDynePanel.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneEditPawn.h"
#include "World/TerraDyneOrchestrator.h"
#include "IO/TerraDyneArchiveHelper.h"
#include "IO/TerraDyneRasterLayers.h"
#include "Building/TerraDyneBuildingActor.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
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
#include "Widgets/Notifications/SProgressBar.h"
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

	auto MakeLayerRow = [this, &SubHeaderFont, &StatusFont, HudText, HudDim, HudLocked, HudAccent, HudCyan, HudRowBG]
		(ETerraDyneRasterLayer Layer, const FText& LayerTitle, bool bRequired) -> TSharedRef<SWidget>
	{
		auto TitleColor = [this, Layer, bRequired, HudLocked, HudAccent, HudText, HudCyan]() -> FSlateColor
		{
			if (const ATerraDyneManager* Mgr = GetManager())
			{
				if (Mgr->IsRasterLayerLoadedInScene(Layer))
				{
					return HudCyan;
				}
			}
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

		auto RowBGColor = [this, Layer, HudRowBG]() -> FSlateColor
		{
			if (const ATerraDyneManager* Mgr = GetManager())
			{
				if (Mgr->IsRasterLayerLoadedInScene(Layer))
				{
					return FLinearColor(0.06f, 0.22f, 0.16f, 0.75f); // Glowing active emerald tint
				}
			}
			return HudRowBG;
		};

		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda(RowBGColor)
			.Padding(FMargin(6.0f, 5.0f))
			[
				SNew(SVerticalBox)
				// Row 1: Header (Title + ACTIVE Badge)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LayerTitle)
							.Font(SubHeaderFont)
							.ColorAndOpacity_Lambda(TitleColor)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ActiveBadge", "● ACTIVE"))
							.Font(StatusFont)
							.ColorAndOpacity(FLinearColor(0.2f, 0.95f, 0.4f))
							.Visibility_Lambda([this, Layer]()
							{
								if (const ATerraDyneManager* Mgr = GetManager())
								{
									if (Mgr->IsRasterLayerLoadedInScene(Layer))
									{
										return EVisibility::Visible;
									}
								}
								return EVisibility::Collapsed;
							})
						]
					]
				]
				// Row 2: File Info / Resolution text
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 4)
				[
					SNew(STextBlock)
					.Text_Lambda([this, Layer]() { return GetLayerStatusText(Layer); })
					.ColorAndOpacity_Lambda([this, Layer]() { return GetLayerStatusColor(Layer); })
					.Font(StatusFont)
					.AutoWrapText(true)
				]
				// Row 3: Action Buttons (Import/Replace, Load/Unload, Clear)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
					[
						SNew(SButton)
						.ButtonStyle(&HudButtonStyle)
						.HAlign(HAlign_Center)
						.Text_Lambda([this, Layer]()
						{
							return IsUnloadLayerEnabled(Layer) ? LOCTEXT("BtnReplace", "Replace…") : LOCTEXT("BtnImport", "Import…");
						})
						.IsEnabled_Lambda([this, Layer]() { return IsImportLayerEnabled(Layer); })
						.OnClicked(this, &STerraDynePanel::OnImportRasterLayerClicked, Layer)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
					[
						SNew(SButton)
						.ButtonStyle(&HudButtonStyle)
						.ButtonColorAndOpacity(this, &STerraDynePanel::GetToggleLayerLoadColor, Layer)
						.HAlign(HAlign_Center)
						.Text_Lambda([this, Layer]() { return GetToggleLayerLoadText(Layer); })
						.ToolTipText(LOCTEXT("BtnToggleLoadTip", "Load or Unload this specific layer into/from the active scene"))
						.IsEnabled_Lambda([this, Layer]() { return IsToggleLayerLoadEnabled(Layer); })
						.OnClicked(this, &STerraDynePanel::OnToggleLayerLoadClicked, Layer)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(&HudButtonStyle)
						.HAlign(HAlign_Center)
						.Text(FText::FromString(TEXT(" ✕ ")))
						.ToolTipText(LOCTEXT("BtnUnloadTip", "Clear / remove this layer file"))
						.IsEnabled_Lambda([this, Layer]() { return IsUnloadLayerEnabled(Layer); })
						.OnClicked(this, &STerraDynePanel::OnUnloadRasterLayerClicked, Layer)
					]
				]
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
			MakeLayerRow(ETerraDyneRasterLayer::Elevation, LOCTEXT("LayerElevation", "1. Elevation (DEM)"), true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.ButtonColorAndOpacity(FLinearColor(0.15f, 0.45f, 0.9f))
				.ToolTipText(LOCTEXT("BtnAddDynamicLayerTip", "Import any GeoTIFF raster (Satellite, Slope, Aspect, Hillshade, Color Relief). Auto-detects type and validates compatibility."))
				.IsEnabled_Lambda([this]()
				{
					if (const ATerraDyneManager* Mgr = GetManager())
					{
						return Mgr->HasElevationLayer();
					}
					return false;
				})
				.OnClicked(this, &STerraDynePanel::OnAddDynamicLayerClicked)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BtnAddDynamicLayer", "+ Add Overlay Layer…"))
					.Font(SubHeaderFont)
					.ColorAndOpacity(FLinearColor::White)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.ToolTipText(LOCTEXT("BtnClearAllLayersTip", "Remove all dynamic overlay layers from the stack"))
				.IsEnabled_Lambda([this]()
				{
					if (const ATerraDyneManager* Mgr = GetManager())
					{
						return Mgr->DynamicRasterLayers.Num() > 0;
					}
					return false;
				})
				.OnClicked(this, &STerraDynePanel::OnClearAllDynamicLayersClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BtnClearAllLayers", "Clear All"))
					.Font(StatusFont)
					.ColorAndOpacity(HudDim)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SAssignNew(DynamicLayersListBox, SVerticalBox)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DEMRequiredFooter", "Elevation (DEM) is required first as reference grid."))
			.Font(LabelFont)
			.ColorAndOpacity(HudDim)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SVerticalBox)
			.Visibility(this, &STerraDynePanel::GetGeoTIFFProgressVisibility)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
			[
				SNew(STextBlock)
				.Text(this, &STerraDynePanel::GetGeoTIFFProgressText)
				.Font(LabelFont)
				.ColorAndOpacity(this, &STerraDynePanel::GetGeoTIFFProgressTextColor)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(SProgressBar)
				.Percent(this, &STerraDynePanel::GetGeoTIFFProgressFraction)
				.Visibility(this, &STerraDynePanel::GetGeoTIFFProgressBarVisibility)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnLoadScene", "Load Scene"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return IsLoadIntoSceneEnabled(); })
				.OnClicked(this, &STerraDynePanel::OnLoadRasterLayersIntoSceneClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(0.9f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnTopView", "Top View (F)"))
				.HAlign(HAlign_Center)
				.OnClicked(this, &STerraDynePanel::OnTopViewClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(0.8f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnUnloadAll", "Unload"))
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
				.ToolTipText(LOCTEXT("BtnRoadAddPointTip", "Click terrain in 3D viewport to draw and extend road"))
				.OnClicked(this, &STerraDynePanel::OnAddRoadPointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRoadRemovePoint", "– Point"))
				.ToolTipText(LOCTEXT("BtnRoadRemovePointTip", "Delete currently selected road spline point"))
				.IsEnabled(this, &STerraDynePanel::HasRoadPointSelected)
				.OnClicked(this, &STerraDynePanel::DeleteRoadPoint)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRoadReset", "Reset"))
				.ToolTipText(LOCTEXT("BtnRoadResetTip", "Clear selected road"))
				.IsEnabled(this, &STerraDynePanel::HasRoadPointSelected)
				.OnClicked(this, &STerraDynePanel::OnResetRoadPointsClicked)
				.HAlign(HAlign_Center)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(this, &STerraDynePanel::GetRoadStatusText)
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
			.Text(this, &STerraDynePanel::GetSelectedBuildingText)
			.Font(LabelFont)
			.ColorAndOpacity(HudText)
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
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &STerraDynePanel::OnAddSplinePointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRemoveSplinePoint", "– Point"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &STerraDynePanel::OnRemoveSplinePointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnResetFootprint", "Reset"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &STerraDynePanel::OnResetFootprintClicked)
				.HAlign(HAlign_Center)
			]
		]
		// Actions: Focus / Relevel / Delete
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnFocusBuilding", "Focus"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &STerraDynePanel::OnFocusBuildingClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRelevelBuilding", "Relevel"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &STerraDynePanel::OnRelevelClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnDeleteBuilding", "Delete"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &STerraDynePanel::OnDeleteBuildingClicked)
				.HAlign(HAlign_Center)
			]
		];

	// 6. Content for TILED CESIUM IMPORT section -- displays local Cesium quantized-mesh
	// terrain and XYZ imagery pyramids alongside TerraDyne's own chunk terrain, entirely
	// offline (file:/// URLs, no ion). Folder-picked each time, never a typed-in path.
	TSharedRef<SWidget> TiledCesiumContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.ToolTipText(LOCTEXT("BtnImportTilesetTip", "Browse to a folder containing layer.json (a Cesium quantized-mesh terrain tileset)."))
			.OnClicked(this, &STerraDynePanel::OnImportCesiumTerrainTilesetClicked)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BtnImportTileset", "Import Terrain Tileset Folder…"))
				.Font(SubHeaderFont)
				.ColorAndOpacity(FLinearColor::White)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.ToolTipText(LOCTEXT("BtnImportOverlayTip", "Browse to the root of an XYZ imagery tile pyramid (z/x/y.png)."))
			.OnClicked(this, &STerraDynePanel::OnImportCesiumImageryOverlayClicked)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BtnImportOverlay", "Import Imagery Overlay Folder…"))
				.Font(SubHeaderFont)
				.ColorAndOpacity(FLinearColor::White)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 1)
		[
			SNew(SCheckBox)
			.IsChecked(this, &STerraDynePanel::IsCutOutBackdropChecked)
			.OnCheckStateChanged(this, &STerraDynePanel::OnCutOutBackdropCheckedChanged)
			.ToolTipText(LOCTEXT("CutOutBackdropTip", "Cut a hole in any other Cesium tileset already in the world (e.g. the default globe backdrop) where the imported terrain sits, so the two meshes don't visibly overlap. Turn off if the computed footprint looks wrong and the cutout lands in the wrong place."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CutOutBackdrop", "Cut Out Backdrop Globe"))
				.Font(LabelFont)
				.ColorAndOpacity(HudDim)
			]
		]
		// HEIGHT OFFSET Slider
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("TiledHeightOffset", "HEIGHT OFFSET")).Font(MicroFont).ColorAndOpacity(HudDim)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &STerraDynePanel::GetTiledCesiumHeightOffsetText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &STerraDynePanel::GetTiledCesiumHeightOffsetSliderValue)
			.OnValueChanged(this, &STerraDynePanel::OnTiledCesiumHeightOffsetSliderChanged)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
		[
			SNew(STextBlock)
			.Text(this, &STerraDynePanel::GetCesiumTiledStatusText)
			.Font(LabelFont)
			.ColorAndOpacity(this, &STerraDynePanel::GetCesiumTiledStatusColor)
			.AutoWrapText(true)
		];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>(this, &STerraDynePanel::GetWindowPadding))
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Left)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(PanelBox, SBox)
				.WidthOverride_Lambda([this]() { return PanelWidth; })
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

						// 6. TILED CESIUM IMPORT SECTION -- DEM worlds only, same gate as GEOTIFF / RASTER LAYERS
						+ SScrollBox::Slot().Padding(0, 1)
						[
							SNew(SBox)
							.Visibility(this, &STerraDynePanel::GetGeoTIFFSectionVisibility)
							[
								MakeCollapsibleSection(LOCTEXT("SecTiledCesium", "TILED CESIUM IMPORT (LOCAL)"), bSectionTiledCesiumOpen, TiledCesiumContent, SectionHeaderFont)
							]
						]
					]
				]
			]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f)
			[
				SNew(SBox)
				.WidthOverride(5.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.25f))
					.ToolTipText(LOCTEXT("ResizeGripTip", "Drag to resize panel width"))
				]
			]
		]
	];

	RefreshDynamicLayersList();
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

FCursorReply STerraDynePanel::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	FVector2D RelativeMouse = LocalMouse - WindowPosition;

	// Check if cursor is over the resize handle on the right edge
	if (RelativeMouse.X >= PanelWidth - 8.0f && RelativeMouse.X <= PanelWidth + 14.0f && RelativeMouse.Y >= 0.0f)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	return FCursorReply::Unhandled();
}

FReply STerraDynePanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D RelativeMouse = LocalMouse - WindowPosition;

		// 1. Resize hit test on the right edge grab bar (within 12px of right edge)
		if (RelativeMouse.X >= PanelWidth - 8.0f && RelativeMouse.X <= PanelWidth + 14.0f && RelativeMouse.Y >= 0.0f)
		{
			bIsResizing = true;
			ResizeStartMouse = LocalMouse;
			InitialPanelWidth = PanelWidth;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// 2. Drag hit test: top header (within top 50px of panel)
		if (RelativeMouse.X >= 0.0f && RelativeMouse.X <= PanelWidth - 8.0f && RelativeMouse.Y >= 0.0f && RelativeMouse.Y <= 50.0f)
		{
			bIsDragging = true;
			DragOffset = RelativeMouse;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// 3. Absorb anything else that lands on the panel so clicks don't sculpt terrain behind it
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
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsResizing || bIsDragging)
		{
			bIsResizing = false;
			bIsDragging = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsResizing)
	{
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float DeltaX = LocalMouse.X - ResizeStartMouse.X;
		PanelWidth = FMath::Clamp(InitialPanelWidth + DeltaX, 280.0f, 900.0f);
		return FReply::Handled();
	}

	if (bIsDragging)
	{
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D NewPos = LocalMouse - DragOffset;

		// Clamp so the panel stays within the viewport
		FVector2D ViewSize = MyGeometry.GetLocalSize();
		NewPos.X = FMath::Clamp(NewPos.X, 0.0f, FMath::Max(0.0f, ViewSize.X - PanelWidth));
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

FReply STerraDynePanel::OnFocusBuildingClicked()
{
	if (ATerraDyneBuildingActor* Building = GetSelectedBuilding())
	{
		APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
		if (!PC && GetManager() && GetManager()->GetWorld())
		{
			PC = GetManager()->GetWorld()->GetFirstPlayerController();
		}
		if (PC)
		{
			if (ATerraDyneEditPawn* Pawn = Cast<ATerraDyneEditPawn>(PC->GetPawn()))
			{
				Pawn->FocusOnLocation(Building->GetActorLocation(), 3500.0f);
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

namespace
{
	// Small persistence helper so each pyramid type remembers its own last-picked folder across
	// editor sessions -- a plain FString member alone resets every time the panel is reconstructed.
	const TCHAR* TiledCesiumImportConfigSection = TEXT("TerraDyne.TiledCesiumImport");
}

FReply STerraDynePanel::OnImportCesiumTerrainTilesetClicked()
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	FString DefaultPath = LastCesiumTerrainFolder;
	if (DefaultPath.IsEmpty())
	{
		GConfig->GetString(TiledCesiumImportConfigSection, TEXT("LastTerrainFolder"), DefaultPath, GEditorPerProjectIni);
	}

	FString PickedFolder;
	const bool bPicked = DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select a Cesium Quantized-Mesh Tileset Folder (containing layer.json)"),
		DefaultPath,
		PickedFolder);

	if (!bPicked)
	{
		return FReply::Handled();
	}

	LastCesiumTerrainFolder = PickedFolder;
	GConfig->SetString(TiledCesiumImportConfigSection, TEXT("LastTerrainFolder"), *PickedFolder, GEditorPerProjectIni);

	if (ATerraDyneManager* Mgr = GetManager())
	{
		FString OutError;
		FVector2D CenterLonLat;
		double SpanDegrees = 0.0;
		if (Mgr->ImportCesiumTerrainTileset(PickedFolder, OutError, CenterLonLat, SpanDegrees))
		{
			LastCesiumTiledStatusMessage = FString::Printf(TEXT("Terrain tileset loaded from %s"), *PickedFolder);
			bLastCesiumTiledStatusIsError = false;
			FlyCameraOverImportedFootprint(CenterLonLat, SpanDegrees);
		}
		else
		{
			LastCesiumTiledStatusMessage = OutError;
			bLastCesiumTiledStatusIsError = true;
		}
	}
#endif

	return FReply::Handled();
}

FReply STerraDynePanel::OnImportCesiumImageryOverlayClicked()
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	FString DefaultPath = LastCesiumOverlayFolder;
	if (DefaultPath.IsEmpty())
	{
		GConfig->GetString(TiledCesiumImportConfigSection, TEXT("LastOverlayFolder"), DefaultPath, GEditorPerProjectIni);
	}

	FString PickedFolder;
	const bool bPicked = DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select an XYZ Imagery Tile Pyramid Folder (containing z/x/y.png)"),
		DefaultPath,
		PickedFolder);

	if (!bPicked)
	{
		return FReply::Handled();
	}

	LastCesiumOverlayFolder = PickedFolder;
	GConfig->SetString(TiledCesiumImportConfigSection, TEXT("LastOverlayFolder"), *PickedFolder, GEditorPerProjectIni);

	if (ATerraDyneManager* Mgr = GetManager())
	{
		FString OutError;
		FVector2D CenterLonLat;
		double SpanDegrees = 0.0;
		if (Mgr->ImportCesiumImageryOverlay(PickedFolder, OutError, CenterLonLat, SpanDegrees))
		{
			LastCesiumTiledStatusMessage = FString::Printf(TEXT("Imagery overlay loaded from %s"), *PickedFolder);
			bLastCesiumTiledStatusIsError = false;
			FlyCameraOverImportedFootprint(CenterLonLat, SpanDegrees);
		}
		else
		{
			LastCesiumTiledStatusMessage = OutError;
			bLastCesiumTiledStatusIsError = true;
		}
	}
#endif

	return FReply::Handled();
}

void STerraDynePanel::FlyCameraOverImportedFootprint(FVector2D CenterLonLat, double SpanDegrees)
{
	if (SpanDegrees <= 0.0)
	{
		// The importer couldn't determine a footprint (e.g. an empty pyramid) -- nowhere sensible to
		// fly to, so leave the camera where it is rather than flying to (0,0) off the coast of Africa.
		return;
	}

	APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
	if (!PC && GetManager() && GetManager()->GetWorld())
	{
		PC = GetManager()->GetWorld()->GetFirstPlayerController();
	}

	ATerraDyneEditPawn* Pawn = PC ? Cast<ATerraDyneEditPawn>(PC->GetPawn()) : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Degrees -> metres via the standard ~111.32 km/degree, then framed with headroom so the whole
	// footprint sits comfortably in view rather than filling the frame edge-to-edge, and floored so a
	// single-tile (near-zero span) import still gets a sensible aerial altitude rather than a
	// ground-level swoop into the terrain.
	const double SpanMeters = SpanDegrees * 111320.0;
	const double Altitude = FMath::Max(3000.0, SpanMeters * 0.9);

	// Same Yaw/Pitch convention as ATerraDyneEditPawn::FocusOnMap's existing "Top View" framing.
	Pawn->FlyToCoordinates(CenterLonLat.X, CenterLonLat.Y, Altitude, /*Yaw=*/90.0, /*Pitch=*/-80.0);
}

ECheckBoxState STerraDynePanel::IsCutOutBackdropChecked() const
{
	const ATerraDyneManager* Mgr = GetManager();
	return (Mgr && Mgr->GetCutOutBackdropForTiledImport()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void STerraDynePanel::OnCutOutBackdropCheckedChanged(ECheckBoxState NewState)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->SetCutOutBackdropForTiledImport(NewState == ECheckBoxState::Checked);
	}
}

float STerraDynePanel::GetTiledCesiumHeightOffsetSliderValue() const
{
	const ATerraDyneManager* Mgr = GetManager();
	const double OffsetMeters = Mgr ? Mgr->GetTiledCesiumHeightOffset() : 0.0;
	return (float)FMath::GetMappedRangeValueClamped(FVector2D(-500.0, 500.0), FVector2D(0.0, 1.0), OffsetMeters);
}

void STerraDynePanel::OnTiledCesiumHeightOffsetSliderChanged(float NewValue)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		const double OffsetMeters = FMath::GetMappedRangeValueClamped(FVector2D(0.0, 1.0), FVector2D(-500.0, 500.0), (double)NewValue);
		Mgr->SetTiledCesiumHeightOffset(OffsetMeters);
	}
}

FText STerraDynePanel::GetTiledCesiumHeightOffsetText() const
{
	const ATerraDyneManager* Mgr = GetManager();
	const double OffsetMeters = Mgr ? Mgr->GetTiledCesiumHeightOffset() : 0.0;
	return FText::Format(LOCTEXT("TiledHeightOffsetValue", "{0}m"), FText::AsNumber(FMath::RoundToInt(OffsetMeters)));
}

FText STerraDynePanel::GetCesiumTiledStatusText() const
{
	if (!LastCesiumTiledStatusMessage.IsEmpty())
	{
		return FText::FromString(LastCesiumTiledStatusMessage);
	}
	return LOCTEXT("TiledCesiumInitialStatus", "No tileset or overlay loaded yet.");
}

FSlateColor STerraDynePanel::GetCesiumTiledStatusColor() const
{
	if (bLastCesiumTiledStatusIsError)
	{
		return FLinearColor(1.0f, 0.35f, 0.35f); // Red
	}
	if (!LastCesiumTiledStatusMessage.IsEmpty())
	{
		return FLinearColor(0.3f, 0.9f, 0.4f); // Green
	}
	return FLinearColor(0.7f, 0.7f, 0.7f); // Subtle grey
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
		LastRasterStatusMessage = FString::Printf(TEXT("Removed %s."), *TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
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

FReply STerraDynePanel::OnToggleLayerLoadClicked(ETerraDyneRasterLayer Layer)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		FString OutError;
		if (Mgr->IsRasterLayerLoadedInScene(Layer))
		{
			if (Mgr->UnloadSpecificRasterLayer(Layer, OutError))
			{
				LastRasterStatusMessage = FString::Printf(TEXT("Unloaded %s from scene."), *TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
				bLastRasterStatusIsError = false;
			}
			else
			{
				LastRasterStatusMessage = OutError;
				bLastRasterStatusIsError = true;
			}
		}
		else
		{
			if (Mgr->LoadSpecificRasterLayer(Layer, OutError))
			{
				LastRasterStatusMessage = FString::Printf(TEXT("Loaded %s into scene."), *TerraDyneRasterLayers::GetDisplayName(Layer).ToString());
				bLastRasterStatusIsError = false;
				if (TerraDyneRasterLayers::IsElevation(Layer))
				{
					OnTopViewClicked();
				}
			}
			else
			{
				LastRasterStatusMessage = OutError;
				bLastRasterStatusIsError = true;
			}
		}
	}
	else if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestLoadSpecificRasterLayer(Layer);
		}
	}
	return FReply::Handled();
}

bool STerraDynePanel::IsToggleLayerLoadEnabled(ETerraDyneRasterLayer Layer) const
{
	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr)
	{
		return false;
	}

	if (!Mgr->IsRasterLayerImported(Layer))
	{
		return false;
	}

	if (!TerraDyneRasterLayers::IsElevation(Layer))
	{
		// Overlays require Elevation to be loaded in scene
		return Mgr->IsRasterLayerLoadedInScene(ETerraDyneRasterLayer::Elevation);
	}

	return true;
}

FText STerraDynePanel::GetToggleLayerLoadText(ETerraDyneRasterLayer Layer) const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->IsRasterLayerLoadedInScene(Layer))
		{
			return LOCTEXT("BtnUnloadSpecific", "Unload");
		}
	}
	return LOCTEXT("BtnLoadSpecific", "Load");
}

FSlateColor STerraDynePanel::GetToggleLayerLoadColor(ETerraDyneRasterLayer Layer) const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->IsRasterLayerLoadedInScene(Layer))
		{
			return FLinearColor(0.9f, 0.45f, 0.1f);
		}
	}
	return FLinearColor(0.2f, 0.8f, 0.3f);
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
			OnTopViewClicked();
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

FReply STerraDynePanel::OnTopViewClicked()
{
	APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
	if (!PC && GetManager() && GetManager()->GetWorld())
	{
		PC = GetManager()->GetWorld()->GetFirstPlayerController();
	}

	if (PC)
	{
		if (ATerraDyneEditPawn* Pawn = Cast<ATerraDyneEditPawn>(PC->GetPawn()))
		{
			Pawn->FocusOnMap();
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

TOptional<float> STerraDynePanel::GetGeoTIFFProgressFraction() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		int32 Loaded = 0;
		int32 Total = 0;
		return Mgr->GetGeoTIFFLoadProgress(Loaded, Total);
	}
	return 0.0f;
}

FText STerraDynePanel::GetGeoTIFFProgressText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->HasActiveVisualOverlay())
		{
			return FText::Format(LOCTEXT("OverlayActiveFmt", "Active Visual: {0} ({1} chunks)"),
				FText::FromString(Mgr->GetActiveVisualOverlayName()),
				FText::AsNumber(Mgr->GetActiveChunkCount()));
		}

		int32 Loaded = 0;
		int32 Total = 0;
		const float Fraction = Mgr->GetGeoTIFFLoadProgress(Loaded, Total);
		if (Total > 0)
		{
			if (Loaded >= Total)
			{
				return FText::Format(LOCTEXT("DEMCompleteFmt", "All {0} DEM chunks loaded (100%)"), FText::AsNumber(Total));
			}
			return FText::Format(LOCTEXT("DEMProgressFmt", "Loading DEM chunks: {0} / {1} ({2}%)"),
				FText::AsNumber(Loaded),
				FText::AsNumber(Total),
				FText::AsNumber(FMath::RoundToInt(Fraction * 100.0f)));
		}
	}
	return FText::GetEmpty();
}

FSlateColor STerraDynePanel::GetGeoTIFFProgressTextColor() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->HasActiveVisualOverlay())
		{
			return FLinearColor(0.2f, 0.95f, 0.45f); // Emerald Green for active overlay
		}

		int32 Loaded = 0;
		int32 Total = 0;
		Mgr->GetGeoTIFFLoadProgress(Loaded, Total);
		if (Total > 0 && Loaded >= Total)
		{
			return FLinearColor(0.2f, 0.9f, 0.4f); // Green when 100% complete
		}
	}
	return FLinearColor(0.2f, 0.7f, 1.0f); // Blue while actively loading
}

EVisibility STerraDynePanel::GetGeoTIFFProgressVisibility() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->GetImportedChunkCoords().Num() > 0)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility STerraDynePanel::GetGeoTIFFProgressBarVisibility() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		int32 Loaded = 0;
		int32 Total = 0;
		Mgr->GetGeoTIFFLoadProgress(Loaded, Total);
		// Collapse the animated bar once 100% is reached so the marquee animation stops
		if (Total > 0 && Loaded < Total)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
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

void STerraDynePanel::ShowNotificationModal(const FText& Title, const FText& Message, bool bIsError)
{
#if WITH_EDITOR
	FMessageDialog::Open(
		EAppMsgType::Ok,
		Message,
		Title);
#else
	LastRasterStatusMessage = Message.ToString();
	bLastRasterStatusIsError = bIsError;
#endif
}

FReply STerraDynePanel::OnAddDynamicLayerClicked()
{
	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr)
	{
		ShowNotificationModal(LOCTEXT("ErrorTitle", "Error"), LOCTEXT("NoManagerErr", "TerraDyne Manager is unavailable."), true);
		return FReply::Handled();
	}

#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Add Overlay Layer (Auto-Detect)"),
			TEXT(""),
			TEXT(""),
			TEXT("GeoTIFF & Raster Archives (*.tif;*.tiff;*.tar.gz;*.tgz;*.zip)|*.tif;*.tiff;*.tar.gz;*.tgz;*.zip|GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|Archives (*.tar.gz;*.tgz;*.zip)|*.tar.gz;*.tgz;*.zip|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			FString ResolvedPath;
			FString ExtractError;
			if (!FTerraDyneArchiveHelper::ResolveOrExtractRaster(OutFiles[0], ETerraDyneRasterLayer::Satellite, ResolvedPath, ExtractError))
			{
				ShowNotificationModal(
					LOCTEXT("ExtractErrorTitle", "Archive Extraction Error"),
					FText::FromString(ExtractError),
					true);
				return FReply::Handled();
			}

			FTerraDyneDynamicRasterLayer AddedLayer;
			FString OutError;
			if (Mgr->AddDynamicRasterLayer(ResolvedPath, AddedLayer, OutError))
			{
				LastRasterStatusMessage = FString::Printf(TEXT("Added [%s]: %s"),
					*AddedLayer.DisplayName, *FPaths::GetCleanFilename(ResolvedPath));
				bLastRasterStatusIsError = false;
				RefreshDynamicLayersList();
			}
			else
			{
				ShowNotificationModal(
					LOCTEXT("LayerImportErrorTitle", "Layer Import / Duplicate Warning"),
					FText::FromString(OutError),
					true);
				LastRasterStatusMessage = OutError;
				bLastRasterStatusIsError = true;
			}
		}
	}
#endif
	return FReply::Handled();
}

FReply STerraDynePanel::OnRemoveDynamicLayerClicked(int32 Index)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->RemoveDynamicRasterLayer(Index);
		RefreshDynamicLayersList();
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnClearAllDynamicLayersClicked()
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->ClearAllDynamicRasterLayers();
		RefreshDynamicLayersList();
	}
	return FReply::Handled();
}

FReply STerraDynePanel::OnToggleDynamicLayerLoadClicked(int32 Index)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->DynamicRasterLayers.IsValidIndex(Index))
		{
			if (Mgr->DynamicRasterLayers[Index].bIsActiveInScene)
			{
				Mgr->UnloadDynamicRasterLayer(Index);
				LastRasterStatusMessage = FString::Printf(TEXT("Unloaded %s."), *Mgr->DynamicRasterLayers[Index].DisplayName);
				bLastRasterStatusIsError = false;
			}
			else
			{
				FString OutError;
				if (Mgr->LoadDynamicRasterLayer(Index, OutError))
				{
					LastRasterStatusMessage = FString::Printf(TEXT("Loaded %s into scene."), *Mgr->DynamicRasterLayers[Index].DisplayName);
					bLastRasterStatusIsError = false;
				}
				else
				{
					ShowNotificationModal(
						LOCTEXT("LoadErrorTitle", "Layer Load Error"),
						FText::FromString(OutError),
						true);
					LastRasterStatusMessage = OutError;
					bLastRasterStatusIsError = true;
				}
			}
			RefreshDynamicLayersList();
		}
	}
	return FReply::Handled();
}

TSharedRef<SWidget> STerraDynePanel::MakeDynamicLayerRow(int32 Index, const FTerraDyneDynamicRasterLayer& Layer)
{
	const FSlateFontInfo SubHeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo StatusFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	const FSlateColor RowBG = Layer.bIsActiveInScene
		? FSlateColor(FLinearColor(0.06f, 0.22f, 0.16f, 0.75f))
		: FSlateColor(FLinearColor(0.12f, 0.12f, 0.14f, 0.5f));

	FLinearColor BadgeColor = FLinearColor(0.2f, 0.6f, 1.0f); // Default Blue
	switch (Layer.LayerType)
	{
	case ETerraDyneRasterLayer::Satellite:
		BadgeColor = FLinearColor(0.2f, 0.85f, 0.7f); // Cyan/Teal
		break;
	case ETerraDyneRasterLayer::Slope:
		BadgeColor = FLinearColor(0.9f, 0.6f, 0.1f); // Orange
		break;
	case ETerraDyneRasterLayer::Aspect:
		BadgeColor = FLinearColor(0.7f, 0.35f, 0.9f); // Purple
		break;
	case ETerraDyneRasterLayer::Roughness:
		BadgeColor = FLinearColor(0.85f, 0.4f, 0.4f); // Coral
		break;
	case ETerraDyneRasterLayer::Hillshade:
		BadgeColor = FLinearColor(0.6f, 0.7f, 0.8f); // Slate
		break;
	case ETerraDyneRasterLayer::ColorRelief:
	case ETerraDyneRasterLayer::ColorHillshade:
		BadgeColor = FLinearColor(0.3f, 0.85f, 0.3f); // Emerald Green
		break;
	default:
		break;
	}

	const FString SpecsText = FString::Printf(TEXT("%s (%dx%d, %d band%s)"),
		*FPaths::GetCleanFilename(Layer.SourceFilePath),
		Layer.Width, Layer.Height,
		Layer.BandCount, Layer.BandCount > 1 ? TEXT("s") : TEXT(""));

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(RowBG)
		.Padding(FMargin(6.0f, 5.0f))
		[
			SNew(SVerticalBox)
			// Row 1: Header (Badge + ACTIVE status + Delete button)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(BadgeColor * 0.35f)
						.Padding(FMargin(5, 2))
						[
							SNew(STextBlock)
							.Text(FText::FromString(Layer.DisplayName))
							.Font(SubHeaderFont)
							.ColorAndOpacity(BadgeColor)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ActiveBadge", "● ACTIVE"))
						.Font(StatusFont)
						.ColorAndOpacity(FLinearColor(0.2f, 0.95f, 0.4f))
						.Visibility(Layer.bIsActiveInScene ? EVisibility::Visible : EVisibility::Collapsed)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.HAlign(HAlign_Center)
					.Text(FText::FromString(TEXT(" ✕ ")))
					.ToolTipText(LOCTEXT("BtnRemoveDynLayerTip", "Delete this layer from stack"))
					.OnClicked(this, &STerraDynePanel::OnRemoveDynamicLayerClicked, Index)
				]
			]
			// Row 2: Specs text
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(SpecsText))
				.ColorAndOpacity(Layer.bIsActiveInScene ? FLinearColor(0.2f, 0.85f, 0.3f) : FLinearColor(0.7f, 0.7f, 0.7f))
				.Font(StatusFont)
				.AutoWrapText(true)
			]
			// Row 3: Action Button (Load / Unload)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.ButtonColorAndOpacity(Layer.bIsActiveInScene ? FLinearColor(0.9f, 0.45f, 0.1f) : FLinearColor(0.2f, 0.8f, 0.3f))
				.HAlign(HAlign_Center)
				.Text(Layer.bIsActiveInScene ? LOCTEXT("BtnUnload", "Unload from Scene") : LOCTEXT("BtnLoad", "Load into Scene"))
				.ToolTipText(LOCTEXT("BtnToggleLoadTip", "Load or Unload this layer into/from the active scene"))
				.OnClicked(this, &STerraDynePanel::OnToggleDynamicLayerLoadClicked, Index)
			]
		];
}

void STerraDynePanel::RefreshDynamicLayersList()
{
	if (!DynamicLayersListBox.IsValid())
	{
		return;
	}

	DynamicLayersListBox->ClearChildren();

	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr || Mgr->DynamicRasterLayers.Num() == 0)
	{
		const FSlateFontInfo InfoFont = FCoreStyle::GetDefaultFontStyle("Italic", 8);
		DynamicLayersListBox->AddSlot()
			.AutoHeight()
			.Padding(2, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoDynamicLayers", "No overlay layers added yet. Click '+ Add Overlay Layer…' to auto-detect and import satellite, slope, hillshade, or color relief rasters."))
				.Font(InfoFont)
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
				.AutoWrapText(true)
			];
		return;
	}

	for (int32 i = 0; i < Mgr->DynamicRasterLayers.Num(); ++i)
	{
		DynamicLayersListBox->AddSlot()
			.AutoHeight()
			.Padding(0, 1)
			[
				MakeDynamicLayerRow(i, Mgr->DynamicRasterLayers[i])
			];
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
	return SetToolMode(ETerraDyneToolMode::DrawPavedRoad);
}

FReply STerraDynePanel::OnRemoveRoadPointClicked()
{
	return DeleteRoadPoint();
}

FReply STerraDynePanel::OnResetRoadPointsClicked()
{
	RoadPointCount = 0;
	if (OwnerWidget.IsValid())
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->ClearRoadSelection();
		}
	}
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
