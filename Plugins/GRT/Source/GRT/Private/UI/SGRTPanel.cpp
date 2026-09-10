// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "SGRTPanel.h"
#include "UI/GRTToolWidget.h"
#include "Core/GRTManager.h"
#include "Core/GRTEditController.h"
#include "Core/GRTEditPawn.h"
#include "World/GRTOrchestrator.h"
#include "IO/GRTArchiveHelper.h"
#include "IO/GRTRasterLayers.h"
#include "Building/GRTBuildingActor.h"
#include "Building/GRTPlacedModelActor.h"
#include "Core/GRTAssetLibrarySubsystem.h"
#include "IO/GRTModelImporter.h"
#include "Gizmo/GizmoManipulationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateImageBrush.h"
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
#include "SGRTHudStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "GRTUI"

void SGRTPanel::Construct(const FArguments& InArgs)
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
	const FLinearColor BGColor = GRTHud::PanelBG;

	// HUD palette: a compact, state-driven scheme (dim/accent/locked, not a rainbow per row) so a
	// layer's colour tells you what it IS -- required-and-empty, locked, or normal -- rather than
	// which one it happens to be. Approximates this project's Lovable HUD mockup (--hud-text/-dim/
	// -locked/-accent/-cyan in its stylesheet); Slate has no CSS variables, so these are named
	// FLinearColor constants instead of themeable tokens.
	// From the shared HUD palette -- these are LINEAR values, see SGRTHudStyle.h for why they
	// look far darker than the sRGB colours they render as.
	const FLinearColor HudText = GRTHud::Text;
	const FLinearColor HudDim = GRTHud::Dim;
	const FLinearColor HudLocked = GRTHud::Locked;
	const FLinearColor HudAccent = GRTHud::Accent;
	const FLinearColor HudCyan = GRTHud::Cyan;
	const FLinearColor HudRowBG(0.06f, 0.06f, 0.08f, 0.85f);
	const FSlateFontInfo StatusFont = MonospaceFont;

	// Panel-wide button look: dark control fill, no rounded-corner chrome, tight padding -- the
	// single biggest visual difference between Slate's default (large, rounded, light grey) button
	// and the reference's compact HUD control. Built once and shared by every SButton in this panel
	// via .ButtonStyle(&HudButtonStyle) rather than per-button styling, so the whole panel reads as
	// one consistent control language instead of Slate defaults with a few restyled exceptions.
	GRTHud::MakeButtonStyle(HudButtonStyle);
	GRTHud::MakeBadgeButtonStyle(BadgeButtonStyle);

	auto MakeLayerRow = [this, &SubHeaderFont, &StatusFont, HudText, HudDim, HudLocked, HudAccent, HudCyan, HudRowBG]
		(EGRTRasterLayer Layer, const FText& LayerTitle, bool bRequired) -> TSharedRef<SWidget>
	{
		auto TitleColor = [this, Layer, bRequired, HudLocked, HudAccent, HudText, HudCyan]() -> FSlateColor
		{
			if (const AGRTManager* Mgr = GetManager())
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
			if (const AGRTManager* Mgr = GetManager())
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
								if (const AGRTManager* Mgr = GetManager())
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
						.OnClicked(this, &SGRTPanel::OnImportRasterLayerClicked, Layer)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
					[
						SNew(SButton)
						.ButtonStyle(&HudButtonStyle)
						.ButtonColorAndOpacity(this, &SGRTPanel::GetToggleLayerLoadColor, Layer)
						.HAlign(HAlign_Center)
						.Text_Lambda([this, Layer]() { return GetToggleLayerLoadText(Layer); })
						.ToolTipText(LOCTEXT("BtnToggleLoadTip", "Load or Unload this specific layer into/from the active scene"))
						.IsEnabled_Lambda([this, Layer]() { return IsToggleLayerLoadEnabled(Layer); })
						.OnClicked(this, &SGRTPanel::OnToggleLayerLoadClicked, Layer)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(&HudButtonStyle)
						.HAlign(HAlign_Center)
						.Text(FText::FromString(TEXT(" ✕ ")))
						.ToolTipText(LOCTEXT("BtnUnloadTip", "Clear / remove this layer file"))
						.IsEnabled_Lambda([this, Layer]() { return IsUnloadLayerEnabled(Layer); })
						.OnClicked(this, &SGRTPanel::OnUnloadRasterLayerClicked, Layer)
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
				.OnClicked(this, &SGRTPanel::SaveToFile)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnLoadFile", "Load from File"))
				.OnClicked(this, &SGRTPanel::LoadFromFile)
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
			MakeLayerRow(EGRTRasterLayer::Elevation, LOCTEXT("LayerElevation", "1. Elevation (DEM)"), true)
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
					if (const AGRTManager* Mgr = GetManager())
					{
						return Mgr->HasElevationLayer();
					}
					return false;
				})
				.OnClicked(this, &SGRTPanel::OnAddDynamicLayerClicked)
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
					if (const AGRTManager* Mgr = GetManager())
					{
						return Mgr->DynamicRasterLayers.Num() > 0;
					}
					return false;
				})
				.OnClicked(this, &SGRTPanel::OnClearAllDynamicLayersClicked)
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
			.Visibility(this, &SGRTPanel::GetGeoTIFFProgressVisibility)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
			[
				SNew(STextBlock)
				.Text(this, &SGRTPanel::GetGeoTIFFProgressText)
				.Font(LabelFont)
				.ColorAndOpacity(this, &SGRTPanel::GetGeoTIFFProgressTextColor)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(SProgressBar)
				.Percent(this, &SGRTPanel::GetGeoTIFFProgressFraction)
				.Visibility(this, &SGRTPanel::GetGeoTIFFProgressBarVisibility)
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
				.OnClicked(this, &SGRTPanel::OnLoadRasterLayersIntoSceneClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(0.9f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnTopView", "Top View (F)"))
				.HAlign(HAlign_Center)
				.OnClicked(this, &SGRTPanel::OnTopViewClicked)
			]
			+ SHorizontalBox::Slot().FillWidth(0.8f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnUnloadAll", "Unload"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return IsUnloadAllEnabled(); })
				.OnClicked(this, &SGRTPanel::OnUnloadAllRasterLayersClicked)
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
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::SculptRaise); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::SculptRaise)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeLower", "Lower"))
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::SculptLower); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::SculptLower)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeSmooth", "Smooth"))
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::Smooth); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::Smooth)
				.HAlign(HAlign_Center)
			]
		]
		// Row 2: Flatten
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.Text(LOCTEXT("ModeFlatten", "Flatten"))
			.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::Flatten); })
			.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::Flatten)
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
				.OnClicked_Lambda([this](){ return SetActiveLayer(EGRTLayer::Base); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetLayerColor, EGRTLayer::Base)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("LayerSculpt", "Sculpt"))
				.OnClicked_Lambda([this](){ return SetActiveLayer(EGRTLayer::Sculpt); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetLayerColor, EGRTLayer::Sculpt)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("LayerDetail", "Detail"))
				.OnClicked_Lambda([this](){ return SetActiveLayer(EGRTLayer::Detail); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetLayerColor, EGRTLayer::Detail)
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
			.OnClicked(this, &SGRTPanel::ResetActiveLayer)
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
				SNew(STextBlock).Text(this, &SGRTPanel::GetBrushRadiusText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &SGRTPanel::GetBrushRadius)
			.OnValueChanged(this, &SGRTPanel::OnBrushRadiusChanged)
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
				SNew(STextBlock).Text(this, &SGRTPanel::GetBrushStrengthText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &SGRTPanel::GetBrushStrength)
			.OnValueChanged(this, &SGRTPanel::OnBrushStrengthChanged)
		];

	// 3b. Content for GIZMO section -- shared by Buildings and Models, since it's one runtime gizmo
	// and one subsystem's worth of state, not a per-tool setting.
	TSharedRef<SWidget> GizmoContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SGRTPanel::IsGizmoWorldSpaceChecked)
			.OnCheckStateChanged(this, &SGRTPanel::OnGizmoWorldSpaceCheckedChanged)
			.ToolTipText(LOCTEXT("GizmoWorldSpaceTip", "Checked: gizmo axes are always world X/Y/Z. Unchecked: gizmo axes follow the selected object's own rotation (Local space). Hotkey: C."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GizmoWorldSpace", "World Space (C)"))
				.Font(LabelFont)
				.ColorAndOpacity(HudDim)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SGRTPanel::IsGizmoSnappingChecked)
			.OnCheckStateChanged(this, &SGRTPanel::OnGizmoSnappingCheckedChanged)
			.ToolTipText(LOCTEXT("GizmoSnappingTip", "Snaps translate/rotate/scale drags to a fixed increment. Hotkey: G. Hold Ctrl while dragging to invert this just for that drag, without changing the setting."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GizmoSnapping", "Snapping Enabled (G, hold Ctrl to invert)"))
				.Font(LabelFont)
				.ColorAndOpacity(HudDim)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GizmoModeHint", "Mode: Shift+W Translate / Shift+E Rotate / Shift+R Scale"))
			.Font(MicroFont)
			.ColorAndOpacity(HudDim)
			.AutoWrapText(true)
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
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::PlaceBuilding); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::PlaceBuilding)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeSelectBuilding", "Select"))
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::SelectBuilding); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::SelectBuilding)
				.HAlign(HAlign_Center)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(this, &SGRTPanel::GetSelectedBuildingText)
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
				.OnClicked(this, &SGRTPanel::OnRemoveFloorClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnFloorPlus", " + "))
				.OnClicked(this, &SGRTPanel::OnAddFloorClicked)
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
				.OnClicked(this, &SGRTPanel::OnAddSplinePointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRemoveSplinePoint", "– Point"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &SGRTPanel::OnRemoveSplinePointClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnResetFootprint", "Reset"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &SGRTPanel::OnResetFootprintClicked)
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
				.OnClicked(this, &SGRTPanel::OnFocusBuildingClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRelevelBuilding", "Relevel"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &SGRTPanel::OnRelevelClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnDeleteBuilding", "Delete"))
				.IsEnabled_Lambda([this](){ return GetSelectedBuilding() != nullptr; })
				.OnClicked(this, &SGRTPanel::OnDeleteBuildingClicked)
				.HAlign(HAlign_Center)
			]
		];

	// 5b. Content for 3D MODELS & ASSET LIBRARY section
	TSharedRef<SWidget> ModelsContent = SNew(SVerticalBox)
		// Import Buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnImport3DModel", "📥 Import Model..."))
				.ToolTipText(LOCTEXT("BtnImport3DModelTip", "Import 3D model (.obj, .stl, .ply) into the placeable asset library"))
				.OnClicked(this, &SGRTPanel::OnImport3DModelClicked)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnImport3DFolder", "📁 Import Folder..."))
				.ToolTipText(LOCTEXT("BtnImport3DFolderTip", "Batch import all 3D models (.obj, .stl, .ply) from a folder"))
				.OnClicked(this, &SGRTPanel::OnImport3DFolderClicked)
				.HAlign(HAlign_Center)
			]
		]
		// Place / Select, mirroring the Buildings section. Selection is gated on one of these being
		// active -- otherwise clicking a model while sculpting would abandon the stroke.
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModePlaceModel", "Place"))
				.ToolTipText(LOCTEXT("ModePlaceModelTip", "Click the terrain to drop the selected library asset"))
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::PlaceModel); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::PlaceModel)
				.HAlign(HAlign_Center)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("ModeSelectModel", "Select"))
				.ToolTipText(LOCTEXT("ModeSelectModelTip", "Click a placed model to select it and attach the transform gizmo"))
				.OnClicked_Lambda([this](){ return SetToolMode(EGRTToolMode::SelectModel); })
				.ButtonColorAndOpacity(this, &SGRTPanel::GetToolModeColor, EGRTToolMode::SelectModel)
				.HAlign(HAlign_Center)
			]
		]
		// Asset Palette Subheader
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ModelPaletteHeader", "PLACEABLE 3D ASSETS"))
			.Font(MicroFont)
			.ColorAndOpacity(HudDim)
		]
		// Dynamic Model Asset List Box
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SAssignNew(ModelAssetListBox, SVerticalBox)
		];

	// 5b-bis. Content for OPENSTREETMAP section. Buildings become footprint-spline buildings,
	// placed through the level's Cesium georeference, which is why the import refuses to run
	// without one.
	TSharedRef<SWidget> OSMContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.Text(LOCTEXT("BtnImportOSM", "🌍 Import OpenStreetMap (.osm)…"))
			.ToolTipText(LOCTEXT("BtnImportOSMTip", "Import buildings from an OpenStreetMap export. Get one from openstreetmap.org: pan to the area you want, click Export, and save the .osm file."))
			.IsEnabled(this, &SGRTPanel::IsOSMImportEnabled)
			.OnClicked(this, &SGRTPanel::OnImportOSMClicked)
			.HAlign(HAlign_Center)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
		[
			SNew(STextBlock).Text(LOCTEXT("OSMWhatHeader", "WHAT TO IMPORT")).Font(MicroFont).ColorAndOpacity(HudDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SGRTPanel::IsOSMBuildingsChecked)
			.OnCheckStateChanged(this, &SGRTPanel::OnOSMBuildingsCheckedChanged)
			.ToolTipText(LOCTEXT("OSMBuildingsTip", "Closed building=* ways become spline buildings. building:levels sets the storey count, falling back to the height tag."))
			[
				SNew(STextBlock).Text(LOCTEXT("OSMBuildings", "Buildings")).Font(LabelFont).ColorAndOpacity(HudDim)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 2)
		[
			SNew(STextBlock)
			.Text(this, &SGRTPanel::GetOSMStatusText)
			.ColorAndOpacity(this, &SGRTPanel::GetOSMStatusColor)
			.Font(LabelFont)
			.AutoWrapText(true)
		]
		// Import spawns are budgeted a few per tick rather than all in one call (a city-sized
		// extract can be thousands of buildings), so this bar has real progress to show rather than
		// jumping straight from empty to full.
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SProgressBar)
			.Percent(this, &SGRTPanel::GetOSMImportProgressFraction)
			.Visibility(this, &SGRTPanel::GetOSMImportProgressVisibility)
		];

	// 5c. Content for SCENE OUTLINER section
	TSharedRef<SWidget> OutlinerContent = SNew(SVerticalBox)
		// Header row with live counter and refresh
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SGRTPanel::GetOutlinerHeaderCountText)
				.Font(MicroFont)
				.ColorAndOpacity(HudDim)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1)
			[
				SNew(SButton)
				.ButtonStyle(&HudButtonStyle)
				.Text(LOCTEXT("BtnRefreshOutliner", "🔄 Refresh"))
				.OnClicked_Lambda([this]()
				{
					RefreshOutlinerList();
					return FReply::Handled();
				})
				.HAlign(HAlign_Center)
			]
		]
		// Dynamic Outliner List Box
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SAssignNew(OutlinerListBox, SVerticalBox)
		];

	// 6. Content for TILED CESIUM IMPORT section -- displays local Cesium quantized-mesh
	// terrain and XYZ imagery pyramids alongside GRT's own chunk terrain, entirely
	// offline (file:/// URLs, no ion). Folder-picked each time, never a typed-in path.
	TSharedRef<SWidget> TiledCesiumContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(&HudButtonStyle)
			.ToolTipText(LOCTEXT("BtnImportTilesetTip", "Browse to a folder containing layer.json (a Cesium quantized-mesh terrain tileset)."))
			.OnClicked(this, &SGRTPanel::OnImportCesiumTerrainTilesetClicked)
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
			.OnClicked(this, &SGRTPanel::OnImportCesiumImageryOverlayClicked)
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
			.IsChecked(this, &SGRTPanel::IsCutOutBackdropChecked)
			.OnCheckStateChanged(this, &SGRTPanel::OnCutOutBackdropCheckedChanged)
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
				SNew(STextBlock).Text(this, &SGRTPanel::GetTiledCesiumHeightOffsetText).Font(MicroFont).ColorAndOpacity(HudAccent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value(this, &SGRTPanel::GetTiledCesiumHeightOffsetSliderValue)
			.OnValueChanged(this, &SGRTPanel::OnTiledCesiumHeightOffsetSliderChanged)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
		[
			SNew(STextBlock)
			.Text(this, &SGRTPanel::GetCesiumTiledStatusText)
			.Font(LabelFont)
			.ColorAndOpacity(this, &SGRTPanel::GetCesiumTiledStatusColor)
			.AutoWrapText(true)
		];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>(this, &SGRTPanel::GetWindowPadding))
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
								.OnClicked(this, &SGRTPanel::OnHeaderModeBadgeClicked)
								[
									SNew(STextBlock)
									.Text(this, &SGRTPanel::GetHeaderModeBadgeText)
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
								.BorderBackgroundColor(GRTHud::Line)
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
							.Visibility(this, &SGRTPanel::GetGeoTIFFSectionVisibility)
							[
								MakeCollapsibleSection(LOCTEXT("SecGeoTIFF", "GEOTIFF / RASTER LAYERS"), bSectionGeoTIFFOpen, GeoTIFFContent, SectionHeaderFont)
							]
						]

						// 2b. OPENSTREETMAP DATA SECTION -- DEM worlds only
						+ SScrollBox::Slot().Padding(0, 1)
						[
							SNew(SBox)
							.Visibility(this, &SGRTPanel::GetGeoTIFFSectionVisibility)
							[
								MakeCollapsibleSection(LOCTEXT("SecOSM", "OPENSTREETMAP DATA"), bSectionOSMOpen, OSMContent, SectionHeaderFont)
							]
						]

						// 3. SCULPTING SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecSculpting", "SCULPTING"), bSectionSculptOpen, SculptContent, SectionHeaderFont)
						]

						// 3b. GIZMO SECTION -- shared translate/rotate/scale settings for Buildings + Models
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecGizmo", "GIZMO"), bSectionGizmoOpen, GizmoContent, SectionHeaderFont)
						]

						// 5. BUILDINGS SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecBuildings", "BUILDINGS"), bSectionBuildingsOpen, BuildingsContent, SectionHeaderFont)
						]

						// 5b. 3D MODELS & ASSETS SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecModels", "3D MODELS & ASSETS"), bSectionModelsOpen, ModelsContent, SectionHeaderFont)
						]

						// 5c. SCENE OUTLINER SECTION
						+ SScrollBox::Slot().Padding(0, 1)
						[
							MakeCollapsibleSection(LOCTEXT("SecOutliner", "SCENE OUTLINER"), bSectionOutlinerOpen, OutlinerContent, SectionHeaderFont)
						]

						// 6. TILED CESIUM IMPORT SECTION -- DEM worlds only, same gate as GEOTIFF / RASTER LAYERS
						+ SScrollBox::Slot().Padding(0, 1)
						[
							SNew(SBox)
							.Visibility(this, &SGRTPanel::GetGeoTIFFSectionVisibility)
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
	RefreshModelAssetList();
	RefreshOutlinerList();

	if (UGRTAssetLibrarySubsystem* AssetLib = GetAssetLibrarySubsystem())
	{
		AssetLib->OnAssetLibraryChanged.AddSP(this, &SGRTPanel::RefreshModelAssetList);
	}
}

TSharedRef<SWidget> SGRTPanel::MakeCollapsibleSection(
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
						.ColorAndOpacity(GRTHud::Dim)
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

FReply SGRTPanel::OnHeaderModeBadgeClicked()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ShowWorldModeChoiceIfNeeded();
	}
	return FReply::Handled();
}

FText SGRTPanel::GetHeaderModeBadgeText() const
{
	if (const AGRTManager* Mgr = GetManager())
	{
		if (Mgr->WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF || Mgr->HasElevationLayer())
		{
			return LOCTEXT("BadgeDEM", "DEM");
		}
	}
	return LOCTEXT("BadgeSandbox", "SANDBOX");
}

FSlateColor SGRTPanel::GetHeaderModeBadgeColor() const
{
	if (const AGRTManager* Mgr = GetManager())
	{
		if (Mgr->WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF || Mgr->HasElevationLayer())
		{
			return GRTHud::Cyan;
		}
	}
	return FLinearColor(0.2f, 0.55f, 0.95f, 1.0f);
}

EVisibility SGRTPanel::GetGeoTIFFSectionVisibility() const
{
	// Same condition the header badge uses for "DEM" vs "SANDBOX" -- this was a stub that always
	// returned Visible, so the raster-layer UI showed up even in a pure Sandbox session with nothing
	// imported and nothing to import into. Only relevant once the world is actually DEM-based (or a
	// layer is already imported, so a Sandbox->DEM switch mid-session doesn't hide in-progress work).
	if (const AGRTManager* Mgr = GetManager())
	{
		if (Mgr->WorldOriginKind == EGRTChunkOrigin::ImportedGeoTIFF || Mgr->HasElevationLayer())
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

void SGRTPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Keep the outliner honest without rebuilding widgets every frame. Placing, deleting or loading
	// anything used to leave the list stale until its Refresh button was pressed.
	TimeSinceOutlinerPoll += InDeltaTime;
	if (TimeSinceOutlinerPoll >= 0.5f)
	{
		TimeSinceOutlinerPoll = 0.0f;
		const int32 Signature = ComputeOutlinerSignature();
		if (Signature != LastOutlinerSignature)
		{
			LastOutlinerSignature = Signature;
			RefreshOutlinerList();
		}
	}
}

int32 SGRTPanel::ComputeOutlinerSignature() const
{
	UWorld* World = nullptr;
	if (OwnerWidget.IsValid())
	{
		World = OwnerWidget->GetWorld();
	}
	else if (AGRTManager* Mgr = GetManager())
	{
		World = Mgr->GetWorld();
	}

	if (!World)
	{
		return 0;
	}

	// Counts per type rather than a full identity hash: cheap enough to run twice a second, and it
	// catches every add and remove. A rename, or one actor swapped for another of the same type
	// inside a single 0.5s window, still needs the manual Refresh -- an acceptable trade for not
	// walking every actor's state on a timer.
	TArray<AActor*> PlacedActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTPlacedModelActor::StaticClass(), PlacedActors);

	TArray<AActor*> BuildingActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTBuildingActor::StaticClass(), BuildingActors);

	return HashCombine(GetTypeHash(PlacedActors.Num()), GetTypeHash(BuildingActors.Num()));
}

FCursorReply SGRTPanel::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
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

FReply SGRTPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
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

FReply SGRTPanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
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

FReply SGRTPanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
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
AGRTOrchestrator* SGRTPanel::GetOrchestrator() const
{
	if (OwnerWidget.IsValid())
	{
		return Cast<AGRTOrchestrator>(UGameplayStatics::GetActorOfClass(OwnerWidget->GetWorld(), AGRTOrchestrator::StaticClass()));
	}
	return nullptr;
}

//--- Buildings ---

AGRTBuildingActor* SGRTPanel::GetSelectedBuilding() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->SelectedBuilding.Get() : nullptr;
}

bool SGRTPanel::IsBuildingSelected() const
{
	return GetSelectedBuilding() != nullptr;
}

FText SGRTPanel::GetSelectedBuildingText() const
{
	if (const AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		return FText::FromString(FString::Printf(
			TEXT("Selected: %s  (%.0f m tall)"),
			*Building->GetName(),
			Building->GetBuildingHeight() / 100.0f));
	}

	return LOCTEXT("NoBuildingSelected", "No building selected");
}

FText SGRTPanel::GetFloorCountText() const
{
	// Falls back to the pending count so the counter still reads sensibly with nothing selected —
	// that value is what the next placed building will get.
	const int32 Floors = GetSelectedBuilding()
		? GetSelectedBuilding()->FloorCount
		: (OwnerWidget.IsValid() ? OwnerWidget->PendingFloorCount : 0);

	return FText::FromString(FString::Printf(TEXT("%d"), Floors));
}

FReply SGRTPanel::OnAddFloorClicked()
{
	if (!OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	OwnerWidget->PendingFloorCount = FMath::Clamp(OwnerWidget->PendingFloorCount + 1, 1, 50);
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

FReply SGRTPanel::OnRemoveFloorClicked()
{
	if (!OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	OwnerWidget->PendingFloorCount = FMath::Clamp(OwnerWidget->PendingFloorCount - 1, 1, 50);
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

FReply SGRTPanel::OnAddSplinePointClicked()
{
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

FReply SGRTPanel::OnRemoveSplinePointClicked()
{
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

FReply SGRTPanel::OnResetFootprintClicked()
{
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

FReply SGRTPanel::OnFocusBuildingClicked()
{
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
		if (!PC && GetManager() && GetManager()->GetWorld())
		{
			PC = GetManager()->GetWorld()->GetFirstPlayerController();
		}
		if (PC)
		{
			if (AGRTEditPawn* Pawn = Cast<AGRTEditPawn>(PC->GetPawn()))
			{
				Pawn->FocusOnLocation(Building->GetActorLocation(), 3500.0f);
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SGRTPanel::OnRelevelClicked()
{
	if (AGRTBuildingActor* Building = GetSelectedBuilding())
	{
		if (OwnerWidget.IsValid())
		{
			if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

FReply SGRTPanel::OnDeleteBuildingClicked()
{
	AGRTBuildingActor* Building = GetSelectedBuilding();
	if (!Building)
	{
		return FReply::Unhandled();
	}

	// Clear the selection first: Destroy() runs EndPlay, which unregisters the building and
	// rebuilds the terrain pads, and the panel must not be holding it while that happens.
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->SelectedBuilding = nullptr;
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

AGRTManager* SGRTPanel::GetManager() const
{
	if (OwnerWidget.IsValid())
	{
		if (UWorld* World = OwnerWidget->GetWorld())
		{
			// Assuming Manager is a singleton or accessible via Subsystem, but looking for Actor for now
			return Cast<AGRTManager>(UGameplayStatics::GetActorOfClass(World, AGRTManager::StaticClass()));
		}
	}
	return nullptr;
}

UGizmoManipulationSubsystem* SGRTPanel::GetGizmoSubsystem() const
{
	if (OwnerWidget.IsValid())
	{
		if (UWorld* World = OwnerWidget->GetWorld())
		{
			return World->GetSubsystem<UGizmoManipulationSubsystem>();
		}
	}
	return nullptr;
}

ECheckBoxState SGRTPanel::IsGizmoWorldSpaceChecked() const
{
	const UGizmoManipulationSubsystem* Gizmo = GetGizmoSubsystem();
	return (!Gizmo || Gizmo->GetCoordinateSpace() == EGizmoCoordinateSpace::World)
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SGRTPanel::OnGizmoWorldSpaceCheckedChanged(ECheckBoxState NewState)
{
	if (UGizmoManipulationSubsystem* Gizmo = GetGizmoSubsystem())
	{
		Gizmo->SetCoordinateSpace(NewState == ECheckBoxState::Checked ? EGizmoCoordinateSpace::World : EGizmoCoordinateSpace::Local);
	}
}

ECheckBoxState SGRTPanel::IsGizmoSnappingChecked() const
{
	const UGizmoManipulationSubsystem* Gizmo = GetGizmoSubsystem();
	return (Gizmo && Gizmo->IsSnappingEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SGRTPanel::OnGizmoSnappingCheckedChanged(ECheckBoxState NewState)
{
	if (UGizmoManipulationSubsystem* Gizmo = GetGizmoSubsystem())
	{
		Gizmo->SetSnappingEnabled(NewState == ECheckBoxState::Checked);
	}
}

//--- Callbacks ---

FText SGRTPanel::GetBrushRadiusText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("RadiusFmt", "{0}m"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushRadius / 100.f)));
	}
	return LOCTEXT("RadiusNA", "--");
}

FText SGRTPanel::GetBrushStrengthText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("StrengthFmt", "{0}%"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushStrength * 20.0f)));
	}
	return LOCTEXT("StrengthNA", "--");
}

FReply SGRTPanel::StartPersistenceTest()
{
	if (AGRTOrchestrator* Orch = GetOrchestrator())
	{
		Orch->StartPersistenceTest();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SGRTPanel::SaveToFile()
{
	// Checked before the dialog opens: the host is the only machine that can use a local path.
	if (OwnerWidget.IsValid())
	{
		if (const AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			if (!PC->CanAccessLocalFiles())
			{
				ShowNotificationModal(LOCTEXT("HostOnlyTitle", "Host only"),
					LOCTEXT("HostOnlySave", "Only the session host can save the world to a file."), true);
				return FReply::Handled();
			}
		}
	}

	const FString DefaultFolder = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GRTExports")));
	IFileManager::Get().MakeDirectory(*DefaultFolder, true);

	FString TargetPath = FPaths::Combine(DefaultFolder, TEXT("MyLevel.grt"));

#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Save GRT Level"),
			DefaultFolder,
			TEXT("MyLevel.grt"),
			TEXT("GRT Level (*.grt)|*.grt"),
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
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			if (!PC->RequestSaveWorldToFile(TargetPath))
			{
				ShowNotificationModal(LOCTEXT("SaveFailedTitle", "Save failed"),
					FText::Format(LOCTEXT("SaveFailedMsg", "Could not save the world to {0}. See the output log for details."),
						FText::FromString(TargetPath)), true);
			}
			return FReply::Handled();
		}
	}

	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->SaveWorldToFile(TargetPath);
	}
	return FReply::Handled();
}

FReply SGRTPanel::LoadFromFile()
{
	if (OwnerWidget.IsValid())
	{
		if (const AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			if (!PC->CanAccessLocalFiles())
			{
				ShowNotificationModal(LOCTEXT("HostOnlyTitle", "Host only"),
					LOCTEXT("HostOnlyLoad", "Only the session host can load a world from a file."), true);
				return FReply::Handled();
			}
		}
	}

	const FString DefaultFolder = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GRTExports")));

	FString TargetPath = FPaths::Combine(DefaultFolder, TEXT("MyLevel.grt"));

#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Load GRT Level"),
			DefaultFolder,
			TEXT(""),
			TEXT("GRT Level (*.grt)|*.grt"),
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
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			if (!PC->RequestLoadWorldFromFile(TargetPath))
			{
				ShowNotificationModal(LOCTEXT("LoadFailedTitle", "Load failed"),
					FText::Format(LOCTEXT("LoadFailedMsg", "Could not load a world from {0}. See the output log for details."),
						FText::FromString(TargetPath)), true);
			}
			return FReply::Handled();
		}
	}

	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->LoadWorldFromFile(TargetPath);
	}
	return FReply::Handled();
}

FText SGRTPanel::GetExportStatusText() const
{
	return LastExportPath.IsEmpty()
		? LOCTEXT("ExportNone", "No file selected yet")
		: FText::Format(LOCTEXT("ExportLast", "Last: {0}"), FText::FromString(LastExportPath));
}

FReply SGRTPanel::OnImportGeoTIFFClicked()
{
	PickAndImportFileForLayer(EGRTRasterLayer::Elevation);
	return FReply::Handled();
}

namespace
{
	// Small persistence helper so each pyramid type remembers its own last-picked folder across
	// editor sessions -- a plain FString member alone resets every time the panel is reconstructed.
	const TCHAR* TiledCesiumImportConfigSection = TEXT("GRT.TiledCesiumImport");
}

FReply SGRTPanel::OnImportCesiumTerrainTilesetClicked()
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

	if (AGRTManager* Mgr = GetManager())
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

FReply SGRTPanel::OnImportCesiumImageryOverlayClicked()
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

	if (AGRTManager* Mgr = GetManager())
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

void SGRTPanel::FlyCameraOverImportedFootprint(FVector2D CenterLonLat, double SpanDegrees)
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

	AGRTEditPawn* Pawn = PC ? Cast<AGRTEditPawn>(PC->GetPawn()) : nullptr;
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

	// Same Yaw/Pitch convention as AGRTEditPawn::FocusOnMap's existing "Top View" framing.
	Pawn->FlyToCoordinates(CenterLonLat.X, CenterLonLat.Y, Altitude, /*Yaw=*/90.0, /*Pitch=*/-80.0);
}

ECheckBoxState SGRTPanel::IsCutOutBackdropChecked() const
{
	const AGRTManager* Mgr = GetManager();
	return (Mgr && Mgr->GetCutOutBackdropForTiledImport()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SGRTPanel::OnCutOutBackdropCheckedChanged(ECheckBoxState NewState)
{
	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->SetCutOutBackdropForTiledImport(NewState == ECheckBoxState::Checked);
	}
}

float SGRTPanel::GetTiledCesiumHeightOffsetSliderValue() const
{
	const AGRTManager* Mgr = GetManager();
	const double OffsetMeters = Mgr ? Mgr->GetTiledCesiumHeightOffset() : 0.0;
	return (float)FMath::GetMappedRangeValueClamped(FVector2D(-500.0, 500.0), FVector2D(0.0, 1.0), OffsetMeters);
}

void SGRTPanel::OnTiledCesiumHeightOffsetSliderChanged(float NewValue)
{
	if (AGRTManager* Mgr = GetManager())
	{
		const double OffsetMeters = FMath::GetMappedRangeValueClamped(FVector2D(0.0, 1.0), FVector2D(-500.0, 500.0), (double)NewValue);
		Mgr->SetTiledCesiumHeightOffset(OffsetMeters);
	}
}

FText SGRTPanel::GetTiledCesiumHeightOffsetText() const
{
	const AGRTManager* Mgr = GetManager();
	const double OffsetMeters = Mgr ? Mgr->GetTiledCesiumHeightOffset() : 0.0;
	return FText::Format(LOCTEXT("TiledHeightOffsetValue", "{0}m"), FText::AsNumber(FMath::RoundToInt(OffsetMeters)));
}

FText SGRTPanel::GetCesiumTiledStatusText() const
{
	if (!LastCesiumTiledStatusMessage.IsEmpty())
	{
		return FText::FromString(LastCesiumTiledStatusMessage);
	}
	return LOCTEXT("TiledCesiumInitialStatus", "No tileset or overlay loaded yet.");
}

FSlateColor SGRTPanel::GetCesiumTiledStatusColor() const
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

FReply SGRTPanel::OnImportRasterLayerClicked(EGRTRasterLayer Layer)
{
	PickAndImportFileForLayer(Layer);
	return FReply::Handled();
}

FReply SGRTPanel::OnUnloadRasterLayerClicked(EGRTRasterLayer Layer)
{
	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->UnloadRasterLayer(Layer);
		LastRasterStatusMessage = FString::Printf(TEXT("Removed %s."), *GRTRasterLayers::GetDisplayName(Layer).ToString());
		bLastRasterStatusIsError = false;
	}
	else if (OwnerWidget.IsValid())
	{
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestUnloadRasterLayer(Layer);
		}
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnToggleLayerLoadClicked(EGRTRasterLayer Layer)
{
	if (AGRTManager* Mgr = GetManager())
	{
		FString OutError;
		if (Mgr->IsRasterLayerLoadedInScene(Layer))
		{
			if (Mgr->UnloadSpecificRasterLayer(Layer, OutError))
			{
				LastRasterStatusMessage = FString::Printf(TEXT("Unloaded %s from scene."), *GRTRasterLayers::GetDisplayName(Layer).ToString());
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
				LastRasterStatusMessage = FString::Printf(TEXT("Loaded %s into scene."), *GRTRasterLayers::GetDisplayName(Layer).ToString());
				bLastRasterStatusIsError = false;
				if (GRTRasterLayers::IsElevation(Layer))
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
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestLoadSpecificRasterLayer(Layer);
		}
	}
	return FReply::Handled();
}

bool SGRTPanel::IsToggleLayerLoadEnabled(EGRTRasterLayer Layer) const
{
	AGRTManager* Mgr = GetManager();
	if (!Mgr)
	{
		return false;
	}

	if (!Mgr->IsRasterLayerImported(Layer))
	{
		return false;
	}

	if (!GRTRasterLayers::IsElevation(Layer))
	{
		// Overlays require Elevation to be loaded in scene
		return Mgr->IsRasterLayerLoadedInScene(EGRTRasterLayer::Elevation);
	}

	return true;
}

FText SGRTPanel::GetToggleLayerLoadText(EGRTRasterLayer Layer) const
{
	if (AGRTManager* Mgr = GetManager())
	{
		if (Mgr->IsRasterLayerLoadedInScene(Layer))
		{
			return LOCTEXT("BtnUnloadSpecific", "Unload");
		}
	}
	return LOCTEXT("BtnLoadSpecific", "Load");
}

FSlateColor SGRTPanel::GetToggleLayerLoadColor(EGRTRasterLayer Layer) const
{
	if (AGRTManager* Mgr = GetManager())
	{
		if (Mgr->IsRasterLayerLoadedInScene(Layer))
		{
			return FLinearColor(0.9f, 0.45f, 0.1f);
		}
	}
	return FLinearColor(0.2f, 0.8f, 0.3f);
}

FReply SGRTPanel::OnUnloadAllRasterLayersClicked()
{
	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->UnloadAllRasterLayers();
		LastRasterStatusMessage = TEXT("All raster layers unloaded and terrain cleared.");
		bLastRasterStatusIsError = false;
	}
	else if (OwnerWidget.IsValid())
	{
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestUnloadAllRasterLayers();
		}
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnLoadRasterLayersIntoSceneClicked()
{
	if (AGRTManager* Mgr = GetManager())
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
		if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
		{
			PC->RequestLoadRasterLayersIntoScene();
		}
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnTopViewClicked()
{
	APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
	if (!PC && GetManager() && GetManager()->GetWorld())
	{
		PC = GetManager()->GetWorld()->GetFirstPlayerController();
	}

	if (PC)
	{
		if (AGRTEditPawn* Pawn = Cast<AGRTEditPawn>(PC->GetPawn()))
		{
			Pawn->FocusOnMap();
		}
	}
	return FReply::Handled();
}

bool SGRTPanel::IsImportLayerEnabled(EGRTRasterLayer Layer) const
{
	if (Layer == EGRTRasterLayer::Elevation)
	{
		return true;
	}

	if (AGRTManager* Mgr = GetManager())
	{
		return Mgr->HasElevationLayer();
	}
	return false;
}

bool SGRTPanel::IsUnloadLayerEnabled(EGRTRasterLayer Layer) const
{
	if (AGRTManager* Mgr = GetManager())
	{
		const int32 Index = (int32)Layer;
		return Mgr->RasterLayerSlots.IsValidIndex(Index) && Mgr->RasterLayerSlots[Index].bImported;
	}
	return false;
}

bool SGRTPanel::IsLoadIntoSceneEnabled() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		return Mgr->HasElevationLayer();
	}
	return false;
}

bool SGRTPanel::IsUnloadAllEnabled() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		for (const FGRTRasterLayerSlot& Slot : Mgr->RasterLayerSlots)
		{
			if (Slot.bImported)
			{
				return true;
			}
		}
	}
	return false;
}

FText SGRTPanel::GetLayerStatusText(EGRTRasterLayer Layer) const
{
	AGRTManager* Mgr = GetManager();
	if (!Mgr)
	{
		return LOCTEXT("NoManager", "Manager unavailable");
	}

	const int32 Index = (int32)Layer;
	if (!Mgr->RasterLayerSlots.IsValidIndex(Index))
	{
		return LOCTEXT("InvalidSlot", "Invalid Slot");
	}

	const FGRTRasterLayerSlot& Slot = Mgr->RasterLayerSlots[Index];
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

	if (Layer == EGRTRasterLayer::Elevation)
	{
		return LOCTEXT("ElevationRequired", "Required - Import first as base grid");
	}

	if (!Mgr->HasElevationLayer())
	{
		return LOCTEXT("OverlayLocked", "Locked - Import Elevation DEM first");
	}

	return LOCTEXT("OverlayEmpty", "Not imported (optional)");
}

FSlateColor SGRTPanel::GetLayerStatusColor(EGRTRasterLayer Layer) const
{
	AGRTManager* Mgr = GetManager();
	if (!Mgr)
	{
		return FLinearColor::Gray;
	}

	const int32 Index = (int32)Layer;
	if (Mgr->RasterLayerSlots.IsValidIndex(Index) && Mgr->RasterLayerSlots[Index].bImported)
	{
		return FLinearColor(0.2f, 0.85f, 0.3f); // Green
	}

	if (Layer == EGRTRasterLayer::Elevation)
	{
		return FLinearColor(1.0f, 0.75f, 0.2f); // Amber / Warning
	}

	if (!Mgr->HasElevationLayer())
	{
		return FLinearColor(0.45f, 0.45f, 0.45f); // Dim grey
	}

	return FLinearColor(0.65f, 0.65f, 0.65f); // Light grey
}

FText SGRTPanel::GetOverallRasterStatusText() const
{
	if (!LastRasterStatusMessage.IsEmpty())
	{
		return FText::FromString(LastRasterStatusMessage);
	}

	if (AGRTManager* Mgr = GetManager())
	{
		if (Mgr->HasElevationLayer())
		{
			int32 ImportedCount = 0;
			for (const FGRTRasterLayerSlot& Slot : Mgr->RasterLayerSlots)
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

FSlateColor SGRTPanel::GetOverallRasterStatusColor() const
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

TOptional<float> SGRTPanel::GetGeoTIFFProgressFraction() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		int32 Loaded = 0;
		int32 Total = 0;
		return Mgr->GetGeoTIFFLoadProgress(Loaded, Total);
	}
	return 0.0f;
}

FText SGRTPanel::GetGeoTIFFProgressText() const
{
	if (AGRTManager* Mgr = GetManager())
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

FSlateColor SGRTPanel::GetGeoTIFFProgressTextColor() const
{
	if (AGRTManager* Mgr = GetManager())
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

EVisibility SGRTPanel::GetGeoTIFFProgressVisibility() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		if (Mgr->GetImportedChunkCoords().Num() > 0)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility SGRTPanel::GetGeoTIFFProgressBarVisibility() const
{
	if (AGRTManager* Mgr = GetManager())
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

void SGRTPanel::PickAndImportFileForLayer(EGRTRasterLayer Layer)
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const FString Title = FString::Printf(TEXT("Import %s"), *GRTRasterLayers::GetDisplayName(Layer).ToString());
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
			if (!FGRTArchiveHelper::ResolveOrExtractRaster(OutFiles[0], Layer, ResolvedPath, ExtractError))
			{
				LastRasterStatusMessage = FString::Printf(TEXT("Failed to extract: %s"), *ExtractError);
				bLastRasterStatusIsError = true;
				return;
			}

			if (AGRTManager* Mgr = GetManager())
			{
				FString OutError;
				if (Mgr->ImportRasterLayer(Layer, ResolvedPath, OutError))
				{
					LastRasterStatusMessage = FString::Printf(TEXT("Successfully imported %s: %s"),
						*GRTRasterLayers::GetDisplayName(Layer).ToString(),
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
				if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
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

void SGRTPanel::ShowNotificationModal(const FText& Title, const FText& Message, bool bIsError)
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

FReply SGRTPanel::OnAddDynamicLayerClicked()
{
	AGRTManager* Mgr = GetManager();
	if (!Mgr)
	{
		ShowNotificationModal(LOCTEXT("ErrorTitle", "Error"), LOCTEXT("NoManagerErr", "GRT Manager is unavailable."), true);
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
			if (!FGRTArchiveHelper::ResolveOrExtractRaster(OutFiles[0], EGRTRasterLayer::Satellite, ResolvedPath, ExtractError))
			{
				ShowNotificationModal(
					LOCTEXT("ExtractErrorTitle", "Archive Extraction Error"),
					FText::FromString(ExtractError),
					true);
				return FReply::Handled();
			}

			FGRTDynamicRasterLayer AddedLayer;
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

FReply SGRTPanel::OnRemoveDynamicLayerClicked(int32 Index)
{
	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->RemoveDynamicRasterLayer(Index);
		RefreshDynamicLayersList();
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnClearAllDynamicLayersClicked()
{
	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->ClearAllDynamicRasterLayers();
		RefreshDynamicLayersList();
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnToggleDynamicLayerLoadClicked(int32 Index)
{
	if (AGRTManager* Mgr = GetManager())
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

TSharedRef<SWidget> SGRTPanel::MakeDynamicLayerRow(int32 Index, const FGRTDynamicRasterLayer& Layer)
{
	const FSlateFontInfo SubHeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo StatusFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	const FSlateColor RowBG = Layer.bIsActiveInScene
		? FSlateColor(FLinearColor(0.06f, 0.22f, 0.16f, 0.75f))
		: FSlateColor(FLinearColor(0.12f, 0.12f, 0.14f, 0.5f));

	FLinearColor BadgeColor = FLinearColor(0.2f, 0.6f, 1.0f); // Default Blue
	switch (Layer.LayerType)
	{
	case EGRTRasterLayer::Satellite:
		BadgeColor = FLinearColor(0.2f, 0.85f, 0.7f); // Cyan/Teal
		break;
	case EGRTRasterLayer::Slope:
		BadgeColor = FLinearColor(0.9f, 0.6f, 0.1f); // Orange
		break;
	case EGRTRasterLayer::Aspect:
		BadgeColor = FLinearColor(0.7f, 0.35f, 0.9f); // Purple
		break;
	case EGRTRasterLayer::Roughness:
		BadgeColor = FLinearColor(0.85f, 0.4f, 0.4f); // Coral
		break;
	case EGRTRasterLayer::Hillshade:
		BadgeColor = FLinearColor(0.6f, 0.7f, 0.8f); // Slate
		break;
	case EGRTRasterLayer::ColorRelief:
	case EGRTRasterLayer::ColorHillshade:
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
					.OnClicked(this, &SGRTPanel::OnRemoveDynamicLayerClicked, Index)
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
				.OnClicked(this, &SGRTPanel::OnToggleDynamicLayerLoadClicked, Index)
			]
		];
}

void SGRTPanel::RefreshDynamicLayersList()
{
	if (!DynamicLayersListBox.IsValid())
	{
		return;
	}

	DynamicLayersListBox->ClearChildren();

	AGRTManager* Mgr = GetManager();
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

FReply SGRTPanel::StartShowcase()
{
	AGRTOrchestrator* Orch = GetOrchestrator();
	if (!Orch && OwnerWidget.IsValid() && OwnerWidget->GetWorld())
	{
		Orch = OwnerWidget->GetWorld()->SpawnActor<AGRTOrchestrator>(
			AGRTOrchestrator::StaticClass(),
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

FReply SGRTPanel::SetActiveLayer(EGRTLayer NewLayer)
{
	if (AGRTManager* Mgr = GetManager())
	{
		Mgr->ActiveLayer = NewLayer;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor SGRTPanel::GetLayerColor(EGRTLayer Layer) const
{
	if (AGRTManager* Mgr = GetManager())
	{
		if (Mgr->ActiveLayer == Layer)
		{
			return GRTHud::ActiveButtonTint;
		}
	}
	return GRTHud::InactiveButtonTint;
}

FReply SGRTPanel::ResetActiveLayer()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ResetActiveLayer();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SGRTPanel::SetToolMode(EGRTToolMode NewMode)
{
	if (OwnerWidget.IsValid())
	{
		// Leaving a model tool should not leave its gizmo and selection outline hanging on screen
		// while a completely different tool is active.
		if (IsModelToolMode(OwnerWidget->CurrentTool) && !IsModelToolMode(NewMode))
		{
			if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->ClearModelSelection();
			}
		}

		OwnerWidget->CurrentTool = NewMode;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor SGRTPanel::GetToolModeColor(EGRTToolMode Mode) const
{
	if (OwnerWidget.IsValid())
	{
		if (OwnerWidget->CurrentTool == Mode)
		{
			return GRTHud::ActiveButtonTint;
		}
	}
	return GRTHud::InactiveButtonTint;
}

float SGRTPanel::GetBrushRadius() const
{
	return OwnerWidget.IsValid() ? (OwnerWidget->BrushRadius / 10000.0f) : 0.2f;
}

void SGRTPanel::OnBrushRadiusChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushRadius = FMath::Clamp(NewValue * 10000.0f, 100.0f, 10000.0f);
	}
}

float SGRTPanel::GetBrushStrength() const
{
	return OwnerWidget.IsValid() ? (OwnerWidget->BrushStrength / 5.0f) : 0.1f;
}

void SGRTPanel::OnBrushStrengthChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushStrength = FMath::Clamp(NewValue * 5.0f, 0.0f, 5.0f);
	}
}

FReply SGRTPanel::OnAddBuildingPointClicked()
{
	BuildingPointCount++;
	return FReply::Handled();
}

FReply SGRTPanel::OnRemoveBuildingPointClicked()
{
	if (BuildingPointCount > 0)
	{
		BuildingPointCount--;
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnResetBuildingPointsClicked()
{
	BuildingPointCount = 0;
	return FReply::Handled();
}

FText SGRTPanel::GetBuildingPointsText() const
{
	return FText::Format(LOCTEXT("BuildingPointsFmt", "{0} points"), FText::AsNumber(BuildingPointCount));
}

FText SGRTPanel::GetStatsText() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		return FText::Format(LOCTEXT("StatsFmt", "Chunks: {0} | Verts: {1}"), 
			FText::AsNumber(Mgr->GetActiveChunkCount()), 
			FText::AsNumber(Mgr->GetTotalVertexCount()));
	}
	return LOCTEXT("StatsNA", "Terrain Manager Not Found");
}

FText SGRTPanel::GetGPUStatsText() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		FGRTGPUStats Stats = Mgr->GetGPUStats();
		return FText::Format(LOCTEXT("GPUStatsFmt", "{0}\n{1}"), 
			FText::FromString(Stats.ComputeBackend),
			FText::FromString(Stats.AdapterName));
	}
	return FText::GetEmpty();
}

FSlateColor SGRTPanel::GetGPUStatusColor() const
{
	if (AGRTManager* Mgr = GetManager())
	{
		if (Mgr->GetGPUStats().bIsCudaAvailable)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // Blue for hybrid RT-assisted path
		}
	}
	return FLinearColor::Gray;
}

bool SGRTPanel::IsUndoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const AGRTEditController* EditPC = Cast<AGRTEditController>(PC))
		{
			// A client's histories live on the server, so it cannot know; let the server decide.
			if (!EditPC->HasAuthority() || EditPC->HasActorUndo())
			{
				return true;
			}
		}
		if (AGRTManager* Mgr = GetManager())
		{
			return Mgr->CanUndo(PC);
		}
	}
	return false;
}

bool SGRTPanel::IsRedoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const AGRTEditController* EditPC = Cast<AGRTEditController>(PC))
		{
			if (!EditPC->HasAuthority() || EditPC->HasActorRedo())
			{
				return true;
			}
		}
		if (AGRTManager* Mgr = GetManager())
		{
			return Mgr->CanRedo(PC);
		}
	}
	return false;
}

FReply SGRTPanel::OnUndoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		// Same path as Ctrl+Z: building/model edits first, then terrain, run on the server. Going
		// straight to Mgr->Undo skipped the building/model history entirely.
		if (AGRTEditController* EditPC = Cast<AGRTEditController>(PC))
		{
			EditPC->RequestUndo();
			return FReply::Handled();
		}
		// Terrain-only fallback for a game mode whose controller is not an AGRTEditController.
		if (AGRTManager* Mgr = GetManager())
		{
			Mgr->Undo(PC);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SGRTPanel::OnRedoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (AGRTEditController* EditPC = Cast<AGRTEditController>(PC))
		{
			EditPC->RequestRedo();
			return FReply::Handled();
		}
		if (AGRTManager* Mgr = GetManager())
		{
			Mgr->Redo(PC);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

// -----------------------------------------------------------------------------
// 3D Models & Asset Library Implementation
// -----------------------------------------------------------------------------
UGRTAssetLibrarySubsystem* SGRTPanel::GetAssetLibrarySubsystem() const
{
	if (OwnerWidget.IsValid())
	{
		if (UWorld* World = OwnerWidget->GetWorld())
		{
			return World->GetSubsystem<UGRTAssetLibrarySubsystem>();
		}
	}
	if (AGRTManager* Mgr = GetManager())
	{
		if (UWorld* World = Mgr->GetWorld())
		{
			return World->GetSubsystem<UGRTAssetLibrarySubsystem>();
		}
	}
	return nullptr;
}

FReply SGRTPanel::OnImport3DModelClicked()
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			TEXT("Import 3D Model"),
			FPaths::ProjectDir(),
			TEXT(""),
			FGRTModelImporter::GetSupportedFileFilter(),
			EFileDialogFlags::Multiple,
			OutFiles
		);

		if (bPicked && OutFiles.Num() > 0)
		{
			if (UGRTAssetLibrarySubsystem* AssetLib = GetAssetLibrarySubsystem())
			{
				for (const FString& FilePath : OutFiles)
				{
					FString OutError;
					AssetLib->ImportModelFromFile(FilePath, OutError);
				}
				RefreshModelAssetList();
				RefreshOutlinerList();
			}
		}
	}
#endif
	return FReply::Handled();
}

FReply SGRTPanel::OnImport3DFolderClicked()
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		FString OutFolder;
		const bool bPicked = DesktopPlatform->OpenDirectoryDialog(
			ParentWindowWindowHandle,
			TEXT("Import 3D Models Folder"),
			FPaths::ProjectDir(),
			OutFolder
		);

		if (bPicked && !OutFolder.IsEmpty())
		{
			if (UGRTAssetLibrarySubsystem* AssetLib = GetAssetLibrarySubsystem())
			{
				AssetLib->ImportDirectory(OutFolder);
				RefreshModelAssetList();
				RefreshOutlinerList();
			}
		}
	}
#endif
	return FReply::Handled();
}

FReply SGRTPanel::OnPlaceModelClicked(FGuid AssetId)
{
	if (UGRTAssetLibrarySubsystem* AssetLib = GetAssetLibrarySubsystem())
	{
		AssetLib->SetSelectedPlacementAsset(AssetId);
	}
	SetToolMode(EGRTToolMode::PlaceModel);
	return FReply::Handled();
}

FReply SGRTPanel::OnRemoveModelAssetClicked(FGuid AssetId)
{
	if (UGRTAssetLibrarySubsystem* AssetLib = GetAssetLibrarySubsystem())
	{
		AssetLib->RemoveAsset(AssetId);
		RefreshModelAssetList();
		RefreshOutlinerList();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SGRTPanel::MakeModelAssetTile(
	const FGRTImportedModelAsset& Asset,
	const FSlateFontInfo& LabelFont,
	const FSlateFontInfo& MicroFont,
	const FLinearColor& HudText,
	const FLinearColor& HudDim,
	const FLinearColor& HudCyan)
{
	const FGuid AssetId = Asset.AssetId;
	const FString ModelName = Asset.AssetName;
	const int32 Tris = Asset.TriangleCount;
	const int32 Verts = Asset.VertexCount;
	const FVector Dims = Asset.Dimensions / 100.0f; // Convert cm to meters

	TSharedPtr<SWidget> ThumbnailWidget;
	if (Asset.ThumbnailTexture)
	{
		// Owned by the panel, not raw-new'd per tile: SImage does not take ownership of the brush,
		// so every rebuild used to leak one.
		TSharedPtr<FSlateImageBrush> ThumbBrush = MakeShared<FSlateImageBrush>(Asset.ThumbnailTexture, FVector2D(42, 42));
		ModelThumbnailBrushes.Add(ThumbBrush);
		ThumbnailWidget = SNew(SImage).Image(ThumbBrush.Get());
	}
	else
	{
		ThumbnailWidget = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.12f, 0.14f, 0.18f, 0.9f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("3D")))
				.Font(MicroFont)
				.ColorAndOpacity(HudCyan)
			];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.07f, 0.08f, 0.10f, 0.95f))
		.Padding(FMargin(4.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			// Thumbnail box
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				SNew(SBox)
				.WidthOverride(42.0f)
				.HeightOverride(42.0f)
				[
					ThumbnailWidget.ToSharedRef()
				]
			]
			// Info
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(ModelName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(HudText)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("ModelStatsFmt", "{0} tris • {1} verts"), FText::AsNumber(Tris), FText::AsNumber(Verts)))
					.Font(MicroFont)
					.ColorAndOpacity(HudDim)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("ModelDimsFmt", "{0}m × {1}m × {2}m"),
						FText::AsNumber(FMath::RoundToInt(Dims.X * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToInt(Dims.Y * 10.0f) / 10.0f),
						FText::AsNumber(FMath::RoundToInt(Dims.Z * 10.0f) / 10.0f)))
					.Font(MicroFont)
					.ColorAndOpacity(FLinearColor(0.6f, 0.8f, 0.6f))
				]
			]
			// Action buttons
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(1)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text(LOCTEXT("BtnPlaceModel", "Place"))
					.ToolTipText(LOCTEXT("BtnPlaceModelTip", "Enter placement mode to drop this 3D model onto the terrain"))
					.OnClicked_Lambda([this, AssetId]()
					{
						return OnPlaceModelClicked(AssetId);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(1)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text(LOCTEXT("BtnRemoveModel", "X"))
					.ToolTipText(LOCTEXT("BtnRemoveModelTip", "Remove this 3D model from the asset catalog"))
					.OnClicked_Lambda([this, AssetId]()
					{
						return OnRemoveModelAssetClicked(AssetId);
					})
				]
			]
		];
}

void SGRTPanel::RefreshModelAssetList()
{
	if (!ModelAssetListBox.IsValid())
	{
		return;
	}

	ModelAssetListBox->ClearChildren();
	// The SImages referencing these went out with ClearChildren, so the brushes can go now too.
	ModelThumbnailBrushes.Reset();

	UGRTAssetLibrarySubsystem* AssetLib = GetAssetLibrarySubsystem();
	if (!AssetLib || AssetLib->GetAssets().Num() == 0)
	{
		ModelAssetListBox->AddSlot().AutoHeight().Padding(2, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoModelsLoaded", "No 3D models imported yet. Click 'Import 3D Model' to import .obj, .stl, or .ply files."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(GRTHud::Dim)
			.AutoWrapText(true)
		];
		return;
	}

	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo MicroFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FLinearColor HudText = GRTHud::Text;
	const FLinearColor HudDim = GRTHud::Dim;
	const FLinearColor HudCyan = GRTHud::Cyan;

	for (const FGRTImportedModelAsset& Asset : AssetLib->GetAssets())
	{
		ModelAssetListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			MakeModelAssetTile(Asset, LabelFont, MicroFont, HudText, HudDim, HudCyan)
		];
	}
}

// -----------------------------------------------------------------------------
// Scene Outliner Implementation
// -----------------------------------------------------------------------------
FText SGRTPanel::GetOutlinerHeaderCountText() const
{
	UWorld* World = OwnerWidget.IsValid() ? OwnerWidget->GetWorld() : (GetManager() ? GetManager()->GetWorld() : nullptr);
	if (!World) return LOCTEXT("OutlinerZero", "SCENE OUTLINER (0)");

	TArray<AActor*> PlacedActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTPlacedModelActor::StaticClass(), PlacedActors);
	TArray<AActor*> BuildingActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTBuildingActor::StaticClass(), BuildingActors);

	return FText::Format(LOCTEXT("OutlinerCountFmt", "SCENE OUTLINER ({0})"), FText::AsNumber(PlacedActors.Num() + BuildingActors.Num()));
}

void SGRTPanel::RefreshOutlinerList()
{
	if (!OutlinerListBox.IsValid())
	{
		return;
	}

	OutlinerListBox->ClearChildren();

	UWorld* World = nullptr;
	if (OwnerWidget.IsValid())
	{
		World = OwnerWidget->GetWorld();
	}
	else if (AGRTManager* Mgr = GetManager())
	{
		World = Mgr->GetWorld();
	}

	if (!World)
	{
		return;
	}

	TArray<AActor*> PlacedActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTPlacedModelActor::StaticClass(), PlacedActors);

	TArray<AActor*> BuildingActors;
	UGameplayStatics::GetAllActorsOfClass(World, AGRTBuildingActor::StaticClass(), BuildingActors);

	const int32 TotalCount = PlacedActors.Num() + BuildingActors.Num();
	if (TotalCount == 0)
	{
		OutlinerListBox->AddSlot().AutoHeight().Padding(2, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoOutlinerActors", "No placed objects in scene. Place 3D models or buildings to track them here."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(GRTHud::Dim)
			.AutoWrapText(true)
		];
		return;
	}

	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo MicroFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FLinearColor HudText = GRTHud::Text;
	const FLinearColor HudDim = GRTHud::Dim;
	const FLinearColor HudCyan = GRTHud::Cyan;

	// 1. Placed Models
	for (AActor* Actor : PlacedActors)
	{
		if (AGRTPlacedModelActor* ModelActor = Cast<AGRTPlacedModelActor>(Actor))
		{
			const FString Label = ModelActor->CustomLabel.IsEmpty() ? ModelActor->ModelName : ModelActor->CustomLabel;
			OutlinerListBox->AddSlot().AutoHeight().Padding(0, 1)
			[
				MakeOutlinerRow(ModelActor, Label, TEXT("Model"), LabelFont, MicroFont, HudText, HudDim, HudCyan)
			];
		}
	}

	// 2. Buildings
	for (AActor* Actor : BuildingActors)
	{
		if (AGRTBuildingActor* Building = Cast<AGRTBuildingActor>(Actor))
		{
			const FString Label = FString::Printf(TEXT("Building (%d floors)"), Building->FloorCount);
			OutlinerListBox->AddSlot().AutoHeight().Padding(0, 1)
			[
				MakeOutlinerRow(Building, Label, TEXT("Building"), LabelFont, MicroFont, HudText, HudDim, HudCyan)
			];
		}
	}

}

TSharedRef<SWidget> SGRTPanel::MakeOutlinerRow(
	AActor* Actor,
	const FString& Label,
	const FString& TypeTag,
	const FSlateFontInfo& LabelFont,
	const FSlateFontInfo& MicroFont,
	const FLinearColor& HudText,
	const FLinearColor& HudDim,
	const FLinearColor& HudCyan)
{
	TWeakObjectPtr<AActor> ActorPtr = Actor;

	auto IsActorSelected = [this, ActorPtr]() -> bool
	{
		if (!ActorPtr.IsValid() || !OwnerWidget.IsValid()) return false;
		if (OwnerWidget->SelectedModelActor.Get() == ActorPtr.Get()) return true;
		if (OwnerWidget->SelectedBuilding.Get() == ActorPtr.Get()) return true;
		return false;
	};

	auto RowBGColor = [IsActorSelected]() -> FSlateColor
	{
		return IsActorSelected()
			? FSlateColor(FLinearColor(0.15f, 0.25f, 0.40f, 0.9f))
			: FSlateColor(FLinearColor(0.06f, 0.07f, 0.09f, 0.8f));
	};

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda(RowBGColor)
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			// Type badge
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("[%s]"), *TypeTag)))
				.Font(MicroFont)
				.ColorAndOpacity(HudCyan)
			]
			// Actor Label & Selection Click
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, ActorPtr]()
				{
					return OnSelectOutlinerActorClicked(ActorPtr);
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(LabelFont)
					.ColorAndOpacity(HudText)
				]
			]
			// Action buttons: Focus, Visibility, Delete
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				// Focus
				+ SHorizontalBox::Slot().AutoWidth().Padding(1)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text(LOCTEXT("BtnOutlinerFocus", "🎯"))
					.ToolTipText(LOCTEXT("BtnOutlinerFocusTip", "Focus camera on this object"))
					.OnClicked_Lambda([this, ActorPtr]()
					{
						return OnFocusOutlinerActorClicked(ActorPtr);
					})
				]
				// Visibility
				+ SHorizontalBox::Slot().AutoWidth().Padding(1)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text_Lambda([ActorPtr]()
					{
						if (!ActorPtr.IsValid()) return FText::FromString(TEXT("👁"));
						return ActorPtr->IsHidden() ? FText::FromString(TEXT("🚫")) : FText::FromString(TEXT("👁"));
					})
					.ToolTipText(LOCTEXT("BtnOutlinerVisTip", "Toggle visibility in scene"))
					.OnClicked_Lambda([this, ActorPtr]()
					{
						return OnToggleOutlinerVisibilityClicked(ActorPtr);
					})
				]
				// Delete
				+ SHorizontalBox::Slot().AutoWidth().Padding(1)
				[
					SNew(SButton)
					.ButtonStyle(&HudButtonStyle)
					.Text(LOCTEXT("BtnOutlinerDelete", "🗑"))
					.ToolTipText(LOCTEXT("BtnOutlinerDeleteTip", "Delete this object from scene"))
					.OnClicked_Lambda([this, ActorPtr]()
					{
						return OnDeleteOutlinerActorClicked(ActorPtr);
					})
				]
			]
		];
}

FReply SGRTPanel::OnSelectOutlinerActorClicked(TWeakObjectPtr<AActor> ActorPtr)
{
	if (!ActorPtr.IsValid()) return FReply::Unhandled();

	AActor* Target = ActorPtr.Get();
	APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
	AGRTEditController* EditPC = Cast<AGRTEditController>(PC);

	if (AGRTPlacedModelActor* ModelActor = Cast<AGRTPlacedModelActor>(Target))
	{
		if (EditPC)
		{
			EditPC->SelectModelActor(ModelActor);
		}
		else if (OwnerWidget.IsValid())
		{
			OwnerWidget->SelectedModelActor = ModelActor;
			OwnerWidget->SelectedBuilding = nullptr;
		}
	}
	else if (AGRTBuildingActor* Building = Cast<AGRTBuildingActor>(Target))
	{
		if (OwnerWidget.IsValid())
		{
			OwnerWidget->SelectedBuilding = Building;
			OwnerWidget->SelectedModelActor = nullptr;
		}
		if (EditPC)
		{
			EditPC->ClearModelSelection();
		}
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnFocusOutlinerActorClicked(TWeakObjectPtr<AActor> ActorPtr)
{
	if (!ActorPtr.IsValid()) return FReply::Unhandled();

	APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
	if (!PC && GetManager() && GetManager()->GetWorld())
	{
		PC = GetManager()->GetWorld()->GetFirstPlayerController();
	}
	if (PC)
	{
		if (AGRTEditPawn* Pawn = Cast<AGRTEditPawn>(PC->GetPawn()))
		{
			Pawn->FocusOnLocation(ActorPtr->GetActorLocation(), 3500.0f);
		}
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnToggleOutlinerVisibilityClicked(TWeakObjectPtr<AActor> ActorPtr)
{
	if (!ActorPtr.IsValid()) return FReply::Unhandled();

	AActor* Target = ActorPtr.Get();
	AGRTEditController* EditPC = OwnerWidget.IsValid() ? Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()) : nullptr;
	const bool bIsGRTActor = Cast<AGRTPlacedModelActor>(Target) || Cast<AGRTBuildingActor>(Target);

	if (EditPC && bIsGRTActor)
	{
		// Through the controller so the change is made on the server and reaches every machine.
		const AGRTPlacedModelActor* ModelActor = Cast<AGRTPlacedModelActor>(Target);
		const bool bCurrentlyVisible = ModelActor ? ModelActor->bIsVisibleInScene : !Target->IsHidden();
		EditPC->RequestSetActorVisible(Target, !bCurrentlyVisible);
	}
	else if (AGRTPlacedModelActor* ModelActor = Cast<AGRTPlacedModelActor>(Target))
	{
		ModelActor->SetVisibleInScene(!ModelActor->bIsVisibleInScene);
	}
	else
	{
		Target->SetActorHiddenInGame(!Target->IsHidden());
	}
	return FReply::Handled();
}

FReply SGRTPanel::OnDeleteOutlinerActorClicked(TWeakObjectPtr<AActor> ActorPtr)
{
	if (!ActorPtr.IsValid()) return FReply::Unhandled();

	AActor* Target = ActorPtr.Get();
	APlayerController* PC = OwnerWidget.IsValid() ? OwnerWidget->GetOwningPlayer() : nullptr;
	AGRTEditController* EditPC = Cast<AGRTEditController>(PC);

	if (AGRTPlacedModelActor* ModelActor = Cast<AGRTPlacedModelActor>(Target))
	{
		if (EditPC)
		{
			EditPC->RequestDeleteModel(ModelActor);
		}
		else
		{
			ModelActor->Destroy();
		}
	}
	else if (AGRTBuildingActor* Building = Cast<AGRTBuildingActor>(Target))
	{
		if (EditPC)
		{
			EditPC->RequestDeleteBuilding(Building);
		}
		else
		{
			Building->Destroy();
		}
	}
	else
	{
		Target->Destroy();
	}

	RefreshOutlinerList();
	return FReply::Handled();
}

FSlateColor SGRTPanel::GetOutlinerVisibilityColor(TWeakObjectPtr<AActor> ActorPtr) const
{
	if (!ActorPtr.IsValid()) return FSlateColor(GRTHud::Dim);
	return ActorPtr->IsHidden() ? FSlateColor(GRTHud::Dim) : FSlateColor(GRTHud::Cyan);
}


//-----------------------------------------------------------------------------
// OpenStreetMap ingestion
//-----------------------------------------------------------------------------

FReply SGRTPanel::OnImportOSMClicked()
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import OpenStreetMap Export"),
			TEXT(""),
			TEXT(""),
			TEXT("OpenStreetMap Data (*.osm;*.xml)|*.osm;*.xml|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			if (OwnerWidget.IsValid())
			{
				if (AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
				{
					PC->RequestImportOSM(OutFiles[0], OwnerWidget->bOSMImportBuildings);
				}
			}
		}
		return FReply::Handled();
	}
#endif

	if (OwnerWidget.IsValid())
	{
		OwnerWidget->OpenOSMImportPicker();
	}
	return FReply::Handled();
}

ECheckBoxState SGRTPanel::IsOSMBuildingsChecked() const
{
	return (OwnerWidget.IsValid() && OwnerWidget->bOSMImportBuildings) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SGRTPanel::OnOSMBuildingsCheckedChanged(ECheckBoxState NewState)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->bOSMImportBuildings = (NewState == ECheckBoxState::Checked);
	}
}

bool SGRTPanel::IsOSMImportEnabled() const
{
	// Importing nothing would silently do nothing, which reads as a broken button. A second import
	// while one is already draining is refused the same way -- disabling the button here is the
	// visible half of the guard RequestImportOSM itself also enforces.
	if (!OwnerWidget.IsValid() || !OwnerWidget->bOSMImportBuildings)
	{
		return false;
	}
	if (const AGRTEditController* PC = Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()))
	{
		return PC->CanAccessLocalFiles() && !PC->IsOSMImportInProgress();
	}
	return true;
}

FText SGRTPanel::GetOSMStatusText() const
{
	if (OwnerWidget.IsValid() && !OwnerWidget->bOSMImportBuildings)
	{
		return LOCTEXT("OSMNothingSelected", "Select at least one thing to import.");
	}

	if (const AGRTEditController* PC = OwnerWidget.IsValid()
		? Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()) : nullptr)
	{
		if (!PC->CanAccessLocalFiles())
		{
			return LOCTEXT("OSMHostOnly", "Only the session host can import OpenStreetMap files.");
		}

		if (PC->IsOSMImportInProgress())
		{
			int32 Done = 0, Total = 0;
			PC->GetOSMImportProgress(Done, Total);
			return FText::Format(LOCTEXT("OSMImportingFmt", "Importing… {0} / {1}"), FText::AsNumber(Done), FText::AsNumber(Total));
		}

		const FGRTOSMImportSummary Summary = PC->GetLastOSMImportSummary();
		if (Summary.bSuccess || !Summary.ErrorMessage.IsEmpty())
		{
			return FText::FromString(Summary.ToDisplayString());
		}
	}

	return LOCTEXT("OSMIdle", "No OSM data imported yet. Export an area from openstreetmap.org, then import the .osm file.");
}

TOptional<float> SGRTPanel::GetOSMImportProgressFraction() const
{
	if (const AGRTEditController* PC = OwnerWidget.IsValid()
		? Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()) : nullptr)
	{
		int32 Done = 0, Total = 0;
		return PC->GetOSMImportProgress(Done, Total);
	}
	return 0.0f;
}

EVisibility SGRTPanel::GetOSMImportProgressVisibility() const
{
	if (const AGRTEditController* PC = OwnerWidget.IsValid()
		? Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()) : nullptr)
	{
		// Collapse once done so the bar doesn't sit at a permanent full 100% under the status text.
		return PC->IsOSMImportInProgress() ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

FSlateColor SGRTPanel::GetOSMStatusColor() const
{
	if (const AGRTEditController* PC = OwnerWidget.IsValid()
		? Cast<AGRTEditController>(OwnerWidget->GetOwningPlayer()) : nullptr)
	{
		const FGRTOSMImportSummary Summary = PC->GetLastOSMImportSummary();
		if (!Summary.ErrorMessage.IsEmpty() && !Summary.bSuccess)
		{
			return FSlateColor(FLinearColor(0.9f, 0.25f, 0.2f));
		}
		if (Summary.bSuccess)
		{
			return FSlateColor(GRTHud::Cyan);
		}
	}
	return FSlateColor(GRTHud::Dim);
}

#undef LOCTEXT_NAMESPACE
