// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TerraDyneWorldModeChoiceWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTerraDyneOnWorldModeChoiceMade);

/**
 * The very first thing a player sees when bShowWorldModeChoiceOnBeginPlay is on: nothing spawns
 * automatically, this shows instead, and the player explicitly picks Sandbox (today's procedural
 * behaviour) or Import GIS File (Imported mode, via the runtime GeoTIFF file picker).
 *
 * Pure native Slate underneath (STerraDyneWorldModeChoicePanel), same split as
 * UTerraDyneFilePickerWidget -- this class is just the UUserWidget host, re-broadcasting the
 * panel's two buttons as Blueprint-assignable delegates. It does not know how to start either
 * mode itself; the owner (UTerraDyneToolWidget) decides what "Sandbox chosen" / "Import chosen"
 * actually does (RequestStartSandboxMode / opening the GeoTIFF file picker).
 */
UCLASS()
class TERRADYNE_API UTerraDyneWorldModeChoiceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Fired when the player picks Sandbox. */
	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|WorldModeChoice")
	FTerraDyneOnWorldModeChoiceMade OnSandboxChosen;

	/** Fired when the player picks Import GIS File. The owner opens the actual file picker. */
	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|WorldModeChoice")
	FTerraDyneOnWorldModeChoiceMade OnImportChosen;

private:
	TSharedPtr<class STerraDyneWorldModeChoicePanel> Panel;

	void HandlePanelSandboxChosen();
	void HandlePanelImportChosen();
};
