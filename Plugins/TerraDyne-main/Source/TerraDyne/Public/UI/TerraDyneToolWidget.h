// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "TerraDyneToolWidget.generated.h"

UENUM(BlueprintType)
enum class ETerraDyneToolMode : uint8
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

	/** Draw interactive realtime paved road */
	DrawPavedRoad,
	/** Draw interactive realtime rough dirt trail */
	DrawRoughRoad,
	/**
	 * Retired. Junctions are detected where roads cross rather than placed by hand, so nothing
	 * selects this mode any more. Kept so saved assets referencing the value still resolve.
	 */
	PlaceJunction UMETA(Hidden),
	/** Clicks select and drag existing road control points instead of drawing new road. */
	SelectRoad
};

/** Building tools take a single click; sculpt tools apply continuously while held. */
inline bool IsBuildingToolMode(ETerraDyneToolMode Mode)
{
	return Mode == ETerraDyneToolMode::PlaceBuilding || Mode == ETerraDyneToolMode::SelectBuilding;
}

inline bool IsRoadToolMode(ETerraDyneToolMode Mode)
{
	return Mode == ETerraDyneToolMode::DrawPavedRoad
		|| Mode == ETerraDyneToolMode::DrawRoughRoad
		|| Mode == ETerraDyneToolMode::SelectRoad;
}

/**
 * Native logic for the Runtime Level Editor UI.
 * Acts as a host for the Slate STerraDynePanel.
 */
UCLASS()
class TERRADYNE_API UTerraDyneToolWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//--- State Variables ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	ETerraDyneToolMode CurrentTool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	float BrushRadius = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	float BrushStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	float BrushFalloff = 0.5f;

	//--- Buildings ---

	/**
	 * Building the panel is currently editing. Weak, so a building destroyed by any route (delete
	 * button, level teardown, another player) simply reads back as "nothing selected" rather than
	 * leaving the panel driving a dangling pointer.
	 */
	TWeakObjectPtr<class ATerraDyneBuildingActor> SelectedBuilding;

	/** Floor count given to the next building placed. Carried over from the last one edited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Buildings", meta = (ClampMin = "1"))
	int32 PendingFloorCount = 2;

public:
	//--- Native Overrides ---
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	//--- Tool Handlers ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Actions")
	void SaveWorld();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Actions")
	void ResetTerrain();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Actions")
	void ResetActiveLayer();

private:
	TSharedPtr<class STerraDynePanel> MainPanel;
};
