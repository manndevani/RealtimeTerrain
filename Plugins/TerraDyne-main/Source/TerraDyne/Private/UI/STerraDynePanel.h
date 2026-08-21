// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/TerraDyneToolWidget.h"
#include "World/TerraDyneTileData.h"
#include "Styling/SlateTypes.h"

/**
 * Top-level workflow modes for the TerraDyne Control Panel.
 */
enum class ETerraDyneUIMode : uint8
{
	GIS,
	Sculpt,
	Roads,
	Buildings,
	Project
};

/**
 * Native Slate implementation of the TerraDyne Control Panel.
 * Provides a professional, runtime-ready GUI for the plugin.
 */
class STerraDynePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STerraDynePanel) {}
	SLATE_ARGUMENT(TWeakObjectPtr<UTerraDyneToolWidget>, ToolWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	//--- State & References ---
	TWeakObjectPtr<UTerraDyneToolWidget> OwnerWidget;
	
	// Window State
	bool bIsDragging = false;
	FVector2D DragOffset;
	FVector2D WindowPosition = FVector2D(50, 50);

	/**
	 * Shared button look for every SButton in this panel (dark HUD control fill, no rounded chrome).
	 * A member, not a Construct()-local: SButton's .ButtonStyle() stores a raw FButtonStyle*, so the
	 * style object has to outlive Construct() and stay valid for the widget's whole lifetime.
	 */
	FButtonStyle HudButtonStyle;
	FButtonStyle BadgeButtonStyle;

	// Section Collapse States (Lovable Accordion style)
	bool bSectionLevelsOpen = true;
	bool bSectionGeoTIFFOpen = true;
	bool bSectionSculptOpen = true;
	bool bSectionRoadsOpen = true;
	bool bSectionBuildingsOpen = true;
	bool bSectionTelemetryOpen = false;

	// Road UI state
	float RoadWidth = 8.0f;
	int32 RoadPointCount = 0;
	FReply OnAddRoadPointClicked();
	FReply OnRemoveRoadPointClicked();
	FReply OnResetRoadPointsClicked();
	float GetRoadWidth() const;
	void OnRoadWidthChanged(float NewValue);
	FText GetRoadWidthText() const;
	FText GetRoadPointsText() const;

	// Building UI state
	int32 BuildingPointCount = 0;
	FReply OnAddBuildingPointClicked();
	FReply OnRemoveBuildingPointClicked();
	FReply OnResetBuildingPointsClicked();
	FText GetBuildingPointsText() const;

	/**
	 * The panel's visible box. Kept so OnMouseButtonDown can tell whether a click landed on the
	 * panel and absorb it — the widget's own slot spans the whole viewport, so its geometry says
	 * nothing about where the panel actually is.
	 */
	TSharedPtr<class SBox> PanelBox;
	
	// Helper to get the Orchestrator from the World
	class ATerraDyneOrchestrator* GetOrchestrator() const;
	class ATerraDyneManager* GetManager() const;

	//--- Mode Badge & Switcher ---
	FReply OnHeaderModeBadgeClicked();
	FText GetHeaderModeBadgeText() const;
	FSlateColor GetHeaderModeBadgeColor() const;
	EVisibility GetGeoTIFFSectionVisibility() const;

	//--- Collapsible Section Builders ---
	TSharedRef<SWidget> MakeCollapsibleSection(
		const FText& SectionTitle,
		bool& bSectionOpen,
		TSharedRef<SWidget> SectionContent,
		const struct FSlateFontInfo& SectionHeaderFont);

	//--- UI Callbacks ---
	// Undo / Redo
	FReply OnUndoClicked();
	FReply OnRedoClicked();
	bool IsUndoEnabled() const;
	bool IsRedoEnabled() const;

	// Tools
	FReply SetToolMode(ETerraDyneToolMode NewMode);

	FReply DeleteRoadPoint();
	bool HasRoadPointSelected() const;
	FSlateColor GetToolModeColor(ETerraDyneToolMode Mode) const;
	FReply SetActiveLayer(ETerraDyneLayer NewLayer);
	FSlateColor GetLayerColor(ETerraDyneLayer Layer) const;
	FReply ResetActiveLayer();
	float GetBrushRadius() const;
	void OnBrushRadiusChanged(float NewValue);
	FText GetBrushRadiusText() const;
	float GetBrushStrength() const;
	void OnBrushStrengthChanged(float NewValue);
	FText GetBrushStrengthText() const;
	
	// Orchestration
	FReply StartShowcase();
	FReply StartPersistenceTest();

	// Levels (file-based save/load via native OS Save/Open dialogs)
	FString LastExportPath;
	FReply SaveToFile();
	FReply LoadFromFile();
	FText GetExportStatusText() const;
	FReply OnImportGeoTIFFClicked();

	//--- Multi-Layer GeoTIFF / Raster UI ---
	FString LastRasterStatusMessage;
	bool bLastRasterStatusIsError = false;

	FReply OnImportRasterLayerClicked(ETerraDyneRasterLayer Layer);
	FReply OnUnloadRasterLayerClicked(ETerraDyneRasterLayer Layer);
	FReply OnToggleLayerLoadClicked(ETerraDyneRasterLayer Layer);
	FReply OnUnloadAllRasterLayersClicked();
	FReply OnLoadRasterLayersIntoSceneClicked();
	FReply OnTopViewClicked();

	bool IsImportLayerEnabled(ETerraDyneRasterLayer Layer) const;
	bool IsUnloadLayerEnabled(ETerraDyneRasterLayer Layer) const;
	bool IsToggleLayerLoadEnabled(ETerraDyneRasterLayer Layer) const;
	FText GetToggleLayerLoadText(ETerraDyneRasterLayer Layer) const;
	FSlateColor GetToggleLayerLoadColor(ETerraDyneRasterLayer Layer) const;
	bool IsLoadIntoSceneEnabled() const;
	bool IsUnloadAllEnabled() const;

	FText GetLayerStatusText(ETerraDyneRasterLayer Layer) const;
	FSlateColor GetLayerStatusColor(ETerraDyneRasterLayer Layer) const;
	FText GetOverallRasterStatusText() const;
	FSlateColor GetOverallRasterStatusColor() const;

	TOptional<float> GetGeoTIFFProgressFraction() const;
	FText GetGeoTIFFProgressText() const;
	EVisibility GetGeoTIFFProgressVisibility() const;

	void PickAndImportFileForLayer(ETerraDyneRasterLayer Layer);

	// Buildings
	class ATerraDyneBuildingActor* GetSelectedBuilding() const;
	FText GetSelectedBuildingText() const;
	FText GetFloorCountText() const;
	bool IsBuildingSelected() const;
	FReply OnAddFloorClicked();
	FReply OnRemoveFloorClicked();
	FReply OnAddSplinePointClicked();
	FReply OnRemoveSplinePointClicked();
	FReply OnResetFootprintClicked();
	FReply OnRelevelClicked();
	FReply OnDeleteBuildingClicked();

	// Roads & Junctions
	FText GetRoadStatusText() const;

	// Visualization
	FText GetStatsText() const;
	FText GetGPUStatsText() const;
	FSlateColor GetGPUStatusColor() const;

	FMargin GetWindowPadding() const { return FMargin(WindowPosition.X, WindowPosition.Y, 0, 0); }
};
