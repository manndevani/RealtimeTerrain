// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "IO/GRTRasterLayers.h"
#include "GRTToolWidget.generated.h"

UENUM(BlueprintType)
enum class EGRTToolMode : uint8
{
	SculptRaise,
	SculptLower,
	Flatten,
	Smooth,
	Paint,

	/** Next terrain click drops a building. */
	PlaceBuilding,
	/** Clicks select a building instead of sculpting. */
	SelectBuilding,

	/** Next terrain click drops an imported 3D model. */
	PlaceModel,

	/** Retired along with the road system. Kept so saved assets referencing the value still resolve. */
	DrawPavedRoad UMETA(Hidden),
	/** Retired along with the road system. Kept so saved assets referencing the value still resolve. */
	DrawRoughRoad UMETA(Hidden),
	/** Retired. Kept so saved assets referencing the value still resolve. */
	PlaceJunction UMETA(Hidden),
	/** Retired along with the road system. Kept so saved assets referencing the value still resolve. */
	SelectRoad UMETA(Hidden),

	/**
	 * Clicks select an already-placed 3D model instead of placing another.
	 *
	 * Appended rather than slotted next to PlaceModel so no existing serialised value shifts.
	 */
	SelectModel
};

/** Building tools take a single click; sculpt tools apply continuously while held. */
inline bool IsBuildingToolMode(EGRTToolMode Mode)
{
	return Mode == EGRTToolMode::PlaceBuilding || Mode == EGRTToolMode::SelectBuilding;
}

inline bool IsModelToolMode(EGRTToolMode Mode)
{
	return Mode == EGRTToolMode::PlaceModel || Mode == EGRTToolMode::SelectModel;
}

/**
 * Native logic for the Runtime Level Editor UI.
 * Acts as a host for the Slate SGRTPanel.
 */
UCLASS()
class GRT_API UGRTToolWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//--- State Variables ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|State")
	EGRTToolMode CurrentTool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|State")
	float BrushRadius = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|State")
	float BrushStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|State")
	float BrushFalloff = 0.5f;

	//--- Buildings & Models ---

	/**
	 * Building the panel is currently editing. Weak, so a building destroyed by any route (delete
	 * button, level teardown, another player) simply reads back as "nothing selected" rather than
	 * leaving the panel driving a dangling pointer.
	 */
	TWeakObjectPtr<class AGRTBuildingActor> SelectedBuilding;

	/** Placed 3D model actor currently selected in editor/outliner. */
	TWeakObjectPtr<class AGRTPlacedModelActor> SelectedModelActor;

	/** Floor count given to the next building placed. Carried over from the last one edited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|Buildings", meta = (ClampMin = "1"))
	int32 PendingFloorCount = 2;

public:
	//--- Native Overrides ---
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	//--- Tool Handlers ---
	UFUNCTION(BlueprintCallable, Category = "GRT|Actions")
	void SaveWorld();

	UFUNCTION(BlueprintCallable, Category = "GRT|Actions")
	void ResetTerrain();

	UFUNCTION(BlueprintCallable, Category = "GRT|Actions")
	void ResetActiveLayer();

	/** Opens the runtime GeoTIFF file picker so the player can choose a .tif/.tiff to import. */
	UFUNCTION(BlueprintCallable, Category = "GRT|Actions")
	void OpenGeoTIFFImportPicker();

	/** Opens the runtime file picker for a specific raster layer. */
	void OpenRasterLayerImportPicker(EGRTRasterLayer Layer);

	/**
	 * Opens the runtime file picker filtered to .osm documents, and routes the chosen file to
	 * AGRTEditController::RequestImportOSM with the current entity toggles.
	 */
	UFUNCTION(BlueprintCallable, Category = "GRT|Actions")
	void OpenOSMImportPicker();

	/** Which OSM entities the next import brings in. Driven by the panel's checkboxes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GRT|OSM")
	bool bOSMImportBuildings = true;

	/**
	 * Shows the Sandbox/Import GIS File startup choice if (and only if) the manager's
	 * bShowWorldModeChoiceOnBeginPlay is on and the world is still genuinely undecided -- not on a
	 * reload of an already-imported level, not a second call while one is already up. Called once
	 * by AGRTEditController right after it creates this widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "GRT|Actions")
	void ShowWorldModeChoiceIfNeeded();

	/**
	 * Un-hides the tool panel once a world mode has actually resolved.
	 *
	 * ShowWorldModeChoiceIfNeeded() collapses this widget so the startup choice is the only thing on
	 * screen; every path that ends the choice has to call this or the player is left with a world and
	 * no tools.
	 */
	void RevealToolPanelAfterChoice();

private:
	TSharedPtr<class SGRTPanel> MainPanel;

	EGRTRasterLayer PendingImportLayer = EGRTRasterLayer::Elevation;

	/** The picker spawned by OpenGeoTIFFImportPicker(), if one is currently up. */
	UPROPERTY(Transient)
	TObjectPtr<class UGRTFilePickerWidget> ActiveGeoTIFFPicker;

	UFUNCTION()
	void HandleGeoTIFFChosen(const FString& FilePath);

	UFUNCTION()
	void HandleGeoTIFFPickerCancelled();

	/** True while the active picker is an OSM one, so the shared handlers route to the right import. */
	bool bPickerIsForOSM = false;

	UFUNCTION()
	void HandleOSMChosen(const FString& FilePath);

	/** The startup choice spawned by ShowWorldModeChoiceIfNeeded(), if one is currently up. */
	UPROPERTY(Transient)
	TObjectPtr<class UGRTWorldModeChoiceWidget> ActiveWorldModeChoice;

	UFUNCTION()
	void HandleWorldModeSandboxChosen();

	UFUNCTION()
	void HandleWorldModeImportChosen();

	class AGRTManager* GetManager() const;
};
