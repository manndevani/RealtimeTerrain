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
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	//--- State & References ---
	TWeakObjectPtr<UTerraDyneToolWidget> OwnerWidget;
	
	// Window & Resize State
	bool bIsDragging = false;
	FVector2D DragOffset;
	FVector2D WindowPosition = FVector2D(50, 50);

	bool bIsResizing = false;
	FVector2D ResizeStartMouse;
	float InitialPanelWidth = 340.0f;
	float PanelWidth = 340.0f;

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
	bool bSectionGizmoOpen = true;
	bool bSectionRoadsOpen = true;
	bool bSectionBuildingsOpen = true;
	bool bSectionModelsOpen = true;
	bool bSectionOSMOpen = true;
	bool bSectionOutlinerOpen = true;
	bool bSectionTelemetryOpen = false;
	bool bSectionTiledCesiumOpen = true;

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
	class UGizmoManipulationSubsystem* GetGizmoSubsystem() const;

	//--- Gizmo section (shared by Roads and Buildings -- one runtime gizmo, not per-tool state) ---
	ECheckBoxState IsGizmoWorldSpaceChecked() const;
	void OnGizmoWorldSpaceCheckedChanged(ECheckBoxState NewState);
	ECheckBoxState IsGizmoSnappingChecked() const;
	void OnGizmoSnappingCheckedChanged(ECheckBoxState NewState);

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
	FReply OnFocusRoadClicked();
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

	// Dynamic Layers
	FReply OnAddDynamicLayerClicked();
	FReply OnRemoveDynamicLayerClicked(int32 Index);
	FReply OnToggleDynamicLayerLoadClicked(int32 Index);
	FReply OnClearAllDynamicLayersClicked();
	TSharedRef<SWidget> MakeDynamicLayerRow(int32 Index, const struct FTerraDyneDynamicRasterLayer& Layer);
	void RefreshDynamicLayersList();
	void ShowNotificationModal(const FText& Title, const FText& Message, bool bIsError);

	TSharedPtr<class SVerticalBox> DynamicLayersListBox;

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
	FSlateColor GetGeoTIFFProgressTextColor() const;
	EVisibility GetGeoTIFFProgressVisibility() const;
	EVisibility GetGeoTIFFProgressBarVisibility() const;

	void PickAndImportFileForLayer(ETerraDyneRasterLayer Layer);

	//--- Tiled Cesium Import (local quantized-mesh terrain / XYZ imagery pyramids) ---
	// Folder pickers rather than a typed-in path, so nothing points at a machine-specific
	// location baked into source: the user browses to the pyramid root each time, and the last
	// pick per pyramid type is remembered across editor sessions via GConfig (see .cpp).
	FString LastCesiumTerrainFolder;
	FString LastCesiumOverlayFolder;
	FString LastCesiumTiledStatusMessage;
	bool bLastCesiumTiledStatusIsError = false;

	FReply OnImportCesiumTerrainTilesetClicked();
	FReply OnImportCesiumImageryOverlayClicked();
	FText GetCesiumTiledStatusText() const;
	FSlateColor GetCesiumTiledStatusColor() const;

	/** Flies the local player's camera to an aerial view over a just-imported pyramid's footprint. */
	void FlyCameraOverImportedFootprint(FVector2D CenterLonLat, double SpanDegrees);

	ECheckBoxState IsCutOutBackdropChecked() const;
	void OnCutOutBackdropCheckedChanged(ECheckBoxState NewState);

	/** -500m..+500m, mapped to/from the slider's normalized [0,1] range. */
	float GetTiledCesiumHeightOffsetSliderValue() const;
	void OnTiledCesiumHeightOffsetSliderChanged(float NewValue);
	FText GetTiledCesiumHeightOffsetText() const;

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
	FReply OnFocusBuildingClicked();
	FReply OnRelevelClicked();
	FReply OnDeleteBuildingClicked();

	// 3D Models & Asset Library
	class UTerraDyneAssetLibrarySubsystem* GetAssetLibrarySubsystem() const;
	FReply OnImport3DModelClicked();
	FReply OnImport3DFolderClicked();
	FReply OnPlaceModelClicked(FGuid AssetId);
	FReply OnRemoveModelAssetClicked(FGuid AssetId);
	TSharedRef<SWidget> MakeModelAssetTile(const struct FTerraDyneImportedModelAsset& Asset, const struct FSlateFontInfo& LabelFont, const struct FSlateFontInfo& MicroFont, const FLinearColor& HudText, const FLinearColor& HudDim, const FLinearColor& HudCyan);
	void RefreshModelAssetList();
	TSharedPtr<class SVerticalBox> ModelAssetListBox;

	/**
	 * Brushes backing the palette's thumbnails. SImage::Image() stores a raw FSlateBrush* and never
	 * takes ownership, so these must outlive Construct -- they were previously raw-new'd per tile
	 * and leaked on every rebuild, which the newly-automatic refresh would have made continuous.
	 */
	TArray<TSharedPtr<struct FSlateImageBrush>> ModelThumbnailBrushes;

	// OpenStreetMap ingestion
	FReply OnImportOSMClicked();
	ECheckBoxState IsOSMBuildingsChecked() const;
	void OnOSMBuildingsCheckedChanged(ECheckBoxState NewState);
	ECheckBoxState IsOSMRoadsChecked() const;
	void OnOSMRoadsCheckedChanged(ECheckBoxState NewState);
	bool IsOSMImportEnabled() const;
	FText GetOSMStatusText() const;
	FSlateColor GetOSMStatusColor() const;
	TOptional<float> GetOSMImportProgressFraction() const;
	EVisibility GetOSMImportProgressVisibility() const;

	// Scene Outliner
	TSharedRef<SWidget> MakeOutlinerRow(AActor* Actor, const FString& Label, const FString& TypeTag, const struct FSlateFontInfo& LabelFont, const struct FSlateFontInfo& MicroFont, const FLinearColor& HudText, const FLinearColor& HudDim, const FLinearColor& HudCyan);
	void RefreshOutlinerList();
	TSharedPtr<class SVerticalBox> OutlinerListBox;

	/**
	 * Cheap poll so the outliner keeps up with the world on its own instead of only when its
	 * Refresh button is pressed. Rebuilding rows every frame would be wasteful, so Tick recomputes
	 * a per-type count signature twice a second and rebuilds only when it actually differs.
	 */
	float TimeSinceOutlinerPoll = 0.0f;
	int32 LastOutlinerSignature = INDEX_NONE;
	int32 ComputeOutlinerSignature() const;
	FText GetOutlinerHeaderCountText() const;
	FReply OnSelectOutlinerActorClicked(TWeakObjectPtr<AActor> ActorPtr);
	FReply OnFocusOutlinerActorClicked(TWeakObjectPtr<AActor> ActorPtr);
	FReply OnToggleOutlinerVisibilityClicked(TWeakObjectPtr<AActor> ActorPtr);
	FReply OnDeleteOutlinerActorClicked(TWeakObjectPtr<AActor> ActorPtr);
	FSlateColor GetOutlinerVisibilityColor(TWeakObjectPtr<AActor> ActorPtr) const;

	// Roads & Junctions
	FText GetRoadStatusText() const;

	// Visualization
	FText GetStatsText() const;
	FText GetGPUStatsText() const;
	FSlateColor GetGPUStatusColor() const;

	FMargin GetWindowPadding() const { return FMargin(WindowPosition.X, WindowPosition.Y, 0, 0); }
};
