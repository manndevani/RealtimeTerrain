// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDynePanel.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "World/TerraDyneOrchestrator.h"
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
#include "Widgets/Colors/SColorBlock.h"

#define LOCTEXT_NAMESPACE "TerraDyneUI"

void STerraDynePanel::Construct(const FArguments& InArgs)
{
	OwnerWidget = InArgs._ToolWidget;

	// Define some simple styles locally
	FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	FSlateColor TitleColor = FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
	FSlateColor BGColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.9f);
	FSlateColor SectionBGColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.8f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>(this, &STerraDynePanel::GetWindowPadding))
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		[
			SAssignNew(PanelBox, SBox)
			.WidthOverride(300.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BGColor)
				.Padding(10.0f)
				[
					SNew(SVerticalBox)
					
					//--- HEADER (Draggable Area) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 10)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor::Transparent)
						.Padding(2.0f)
						[
							// Empty strip. The panel's drag handle is a coordinate hit test over the
							// top 50 px in OnMouseButtonDown, so this has to keep occupying that
							// space even with the title text gone.
							SNew(SBox)
							.HeightOverride(18.0f)
						]
					]

					//--- LEVELS SECTION (file-based export/import) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("LevelsHeader", "LEVELS")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnSaveFile", "Save to File"))
									.OnClicked(this, &STerraDynePanel::SaveToFile)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnLoadFile", "Load from File"))
									.OnClicked(this, &STerraDynePanel::LoadFromFile)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
							[
								SNew(STextBlock).Text_Lambda([this](){ return GetExportStatusText(); }).Font(LabelFont)
							]
						]
					]

					//--- IMPORT SECTION (GeoTIFF) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("ImportHeader", "IMPORT")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnImportGeoTIFF", "Import GeoTIFF..."))
									.OnClicked(this, &STerraDynePanel::OnImportGeoTIFFClicked)
									.HAlign(HAlign_Center)
								]
							]
						]
					]

					//--- SCULPTING SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("SculptHeader", "SCULPTING")).Font(HeaderFont)
							]
							
							// Tool Modes — Row 1: Raise / Lower / Smooth
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeRaise", "Raise"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptRaise); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptRaise)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeLower", "Lower"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptLower); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptLower)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeSmooth", "Smooth"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Smooth); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Smooth)
								.HAlign(HAlign_Center)
							]
						]
						// Tool Modes — Row 2: Flatten
						// Paint was removed along with its layer picker: the terrain material derives
						// its layer split from vertex colors and no longer samples a WeightMap, so
						// painting a layer had no visual effect.
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeFlatten", "Flatten"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Flatten); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Flatten)
								.HAlign(HAlign_Center)
							]
						]

						// Dynamic Layers
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 5)
							[
								SNew(STextBlock).Text(LOCTEXT("LayerHeader", "DYNAMIC LAYERS")).Font(LabelFont).ColorAndOpacity(FLinearColor::Gray)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerBase", "Base"))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Base); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Base)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerSculpt", "Sculpt"))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Sculpt); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Sculpt)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerDetail", "Detail"))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Detail); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Detail)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnResetLayer", "Reset Active Layer"))
								.OnClicked(this, &STerraDynePanel::ResetActiveLayer)
								.HAlign(HAlign_Center)
							]

							// Sliders
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock).Text(LOCTEXT("LblRadius", "Radius: "))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock)
										.Text(this, &STerraDynePanel::GetBrushRadiusText)
										.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SSlider)
									.MinValue(100.0f)
									.MaxValue(10000.0f)
									.Value(this, &STerraDynePanel::GetBrushRadius)
									.OnValueChanged(this, &STerraDynePanel::OnBrushRadiusChanged)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock).Text(LOCTEXT("LblStrength", "Strength: "))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock)
										.Text(this, &STerraDynePanel::GetBrushStrengthText)
										.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SSlider)
									.MinValue(0.0f)
									.MaxValue(5.0f)
									.Value(this, &STerraDynePanel::GetBrushStrength)
									.OnValueChanged(this, &STerraDynePanel::OnBrushStrengthChanged)
								]
							]
						]
					]

					//--- BUILDINGS SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("BuildingHeader", "BUILDINGS")).Font(HeaderFont)
							]

							// Arm placement / selection. These are tool modes like the sculpt
							// buttons, so the same highlight shows which one is live.
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("ModePlaceBuilding", "Place"))
									.ToolTipText(LOCTEXT("TipPlaceBuilding", "Click the terrain to drop a building there."))
									.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::PlaceBuilding); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::PlaceBuilding)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("ModeSelectBuilding", "Select"))
									.ToolTipText(LOCTEXT("TipSelectBuilding", "Click a building to edit it without placing new ones."))
									.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SelectBuilding); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SelectBuilding)
									.HAlign(HAlign_Center)
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetSelectedBuildingText)
								.Font(LabelFont)
								.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
							]

							// Floor counter
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(LOCTEXT("LblFloors", "Floors: ")).Font(LabelFont)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(this, &STerraDynePanel::GetFloorCountText)
									.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
								]
								+ SHorizontalBox::Slot().AutoWidth().Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnFloorMinus", " - "))
									.OnClicked(this, &STerraDynePanel::OnRemoveFloorClicked)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().AutoWidth().Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnFloorPlus", " + "))
									.OnClicked(this, &STerraDynePanel::OnAddFloorClicked)
									.HAlign(HAlign_Center)
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("FootprintHeader", "FOOTPRINT SPLINE"))
								.Font(LabelFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnAddSplinePoint", "+ Point"))
									.ToolTipText(LOCTEXT("TipAddPoint", "Insert a new corner/vertex point into the building footprint."))
									.IsEnabled(this, &STerraDynePanel::IsBuildingSelected)
									.OnClicked(this, &STerraDynePanel::OnAddSplinePointClicked)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnRemoveSplinePoint", "- Point"))
									.ToolTipText(LOCTEXT("TipRemovePoint", "Remove a corner/vertex point from the building footprint."))
									.IsEnabled(this, &STerraDynePanel::IsBuildingSelected)
									.OnClicked(this, &STerraDynePanel::OnRemoveSplinePointClicked)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnResetFootprint", "↺ Reset"))
									.ToolTipText(LOCTEXT("TipResetFootprint", "Reset footprint back to default 4-corner rectangle."))
									.IsEnabled(this, &STerraDynePanel::IsBuildingSelected)
									.OnClicked(this, &STerraDynePanel::OnResetFootprintClicked)
									.HAlign(HAlign_Center)
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnRelevel", "Re-level"))
									.ToolTipText(LOCTEXT("TipRelevel", "Re-sample the ground under the footprint and reset the pad height."))
									.IsEnabled(this, &STerraDynePanel::IsBuildingSelected)
									.OnClicked(this, &STerraDynePanel::OnRelevelClicked)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnDeleteBuilding", "Delete"))
									.IsEnabled(this, &STerraDynePanel::IsBuildingSelected)
									.OnClicked(this, &STerraDynePanel::OnDeleteBuildingClicked)
									.HAlign(HAlign_Center)
								]
							]
						]
					]

					//--- ROADS & JUNCTIONS SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("RoadsHeader", "ROADS & JUNCTIONS")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("ModePavedRoad", "Paved Road"))
									.ToolTipText(LOCTEXT("TipPavedRoad", "Click and draw 3D paved road with curbs & sidewalks."))
									.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::DrawPavedRoad); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::DrawPavedRoad)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("ModeRoughRoad", "Rough Trail"))
									.ToolTipText(LOCTEXT("TipRoughRoad", "Click and draw dirt trail with scattered rocks."))
									.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::DrawRoughRoad); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::DrawRoughRoad)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("ModeSelectRoad", "Select"))
									.ToolTipText(LOCTEXT("TipSelectRoad", "Click a road to show its points, then drag a point to move it."))
									.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SelectRoad); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SelectRoad)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("RoadUndo", "Undo"))
									.ToolTipText(LOCTEXT("TipRoadUndo", "Take back the last road edit."))
									.OnClicked(this, &STerraDynePanel::OnUndoClicked)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("RoadRedo", "Redo"))
									.ToolTipText(LOCTEXT("TipRoadRedo", "Reapply the last undone road edit."))
									.OnClicked(this, &STerraDynePanel::OnRedoClicked)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.4f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("RoadDeletePoint", "Delete Point"))
									.ToolTipText(LOCTEXT("TipRoadDeletePoint", "Remove the selected control point."))
									.OnClicked(this, &STerraDynePanel::DeleteRoadPoint)
									.IsEnabled(this, &STerraDynePanel::HasRoadPointSelected)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetRoadStatusText)
								.Font(LabelFont)
								.ColorAndOpacity(FLinearColor(0.25f, 0.8f, 0.5f))
							]
						]
					]

					//--- TELEMETRY SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("StatsHeader", "TELEMETRY")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetStatsText)
								.Font(LabelFont)
							]
						]
					]
				]
			]
		]
	];
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
	// Same split as SaveToFile/LoadFromFile above: the native OS dialog is simpler and more
	// familiar than a hand-rolled browser, but DesktopPlatform is only linked in editor builds
	// (see TerraDyne.Build.cs) -- packaged builds fall back to the runtime picker widget.
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import GeoTIFF"),
			TEXT(""),
			TEXT(""),
			TEXT("GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0 && OwnerWidget.IsValid())
		{
			if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(OwnerWidget->GetOwningPlayer()))
			{
				PC->RequestImportGeoTIFF(OutFiles[0]);
			}
		}
		return FReply::Handled();
	}
#endif

	if (OwnerWidget.IsValid())
	{
		OwnerWidget->OpenGeoTIFFImportPicker();
	}
	return FReply::Handled();
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
			return FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
		}
	}
	return FLinearColor::Gray;
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
			return FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
		}
	}
	return FLinearColor::Gray;
}

float STerraDynePanel::GetBrushRadius() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->BrushRadius : 2000.0f;
}

void STerraDynePanel::OnBrushRadiusChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushRadius = FMath::Clamp(NewValue, 100.0f, 10000.0f);
	}
}

float STerraDynePanel::GetBrushStrength() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->BrushStrength : 0.5f;
}

void STerraDynePanel::OnBrushStrengthChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushStrength = FMath::Clamp(NewValue, 0.0f, 5.0f);
	}
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
