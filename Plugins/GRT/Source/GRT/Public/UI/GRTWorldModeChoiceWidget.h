// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GRTWorldModeChoiceWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGRTOnWorldModeChoiceMade);

/**
 * The very first thing a player sees when bShowWorldModeChoiceOnBeginPlay is on: nothing spawns
 * automatically, this shows instead, and the player explicitly picks Sandbox (today's procedural
 * behaviour) or Import GIS File (Imported mode, via the runtime GeoTIFF file picker).
 *
 * Pure native Slate underneath (SGRTWorldModeChoicePanel), same split as
 * UGRTFilePickerWidget -- this class is just the UUserWidget host, re-broadcasting the
 * panel's two buttons as Blueprint-assignable delegates. It does not know how to start either
 * mode itself; the owner (UGRTToolWidget) decides what "Sandbox chosen" / "Import chosen"
 * actually does (RequestStartSandboxMode / opening the GeoTIFF file picker).
 */
UCLASS()
class GRT_API UGRTWorldModeChoiceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Fired when the player picks Sandbox. */
	UPROPERTY(BlueprintAssignable, Category = "GRT|WorldModeChoice")
	FGRTOnWorldModeChoiceMade OnSandboxChosen;

	/** Fired when the player picks Import GIS File. The owner opens the actual file picker. */
	UPROPERTY(BlueprintAssignable, Category = "GRT|WorldModeChoice")
	FGRTOnWorldModeChoiceMade OnImportChosen;

private:
	TSharedPtr<class SGRTWorldModeChoicePanel> Panel;

	void HandlePanelSandboxChosen();
	void HandlePanelImportChosen();
};
