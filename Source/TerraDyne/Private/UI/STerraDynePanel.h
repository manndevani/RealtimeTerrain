// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/TerraDyneToolWidget.h"
#include "World/TerraDyneTileData.h"

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
	 * The panel's visible box. Kept so OnMouseButtonDown can tell whether a click landed on the
	 * panel and absorb it — the widget's own slot spans the whole viewport, so its geometry says
	 * nothing about where the panel actually is.
	 */
	TSharedPtr<class SBox> PanelBox;
	
	// Helper to get the Orchestrator from the World
	class ATerraDyneOrchestrator* GetOrchestrator() const;
	class ATerraDyneManager* GetManager() const;

	//--- UI Callbacks ---
	// Undo / Redo
	FReply OnUndoClicked();
	FReply OnRedoClicked();
	bool IsUndoEnabled() const;
	bool IsRedoEnabled() const;

	// Tools
	FReply SetToolMode(ETerraDyneToolMode NewMode);
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

	// Buildings
	class ATerraDyneBuildingActor* GetSelectedBuilding() const;
	FText GetSelectedBuildingText() const;
	FText GetFloorCountText() const;
	bool IsBuildingSelected() const;
	FReply OnAddFloorClicked();
	FReply OnRemoveFloorClicked();
	FReply OnRelevelClicked();
	FReply OnDeleteBuildingClicked();

	// Visualization
	FText GetStatsText() const;
	FText GetGPUStatsText() const;
	FSlateColor GetGPUStatusColor() const;

	FMargin GetWindowPadding() const { return FMargin(WindowPosition.X, WindowPosition.Y, 0, 0); }
};
