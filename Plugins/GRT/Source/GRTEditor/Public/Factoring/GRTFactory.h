// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "GRTFactory.generated.h"

/**
 * UGRTTileDataFactory
 * 
 * Standard Unreal Engine Factory implementation.
 * Registers "GRT Tile Data" in the "Add" / Right-Click menu of the Content Browser.
 */
UCLASS()
class GRTEDITOR_API UGRTTileDataFactory : public UFactory
{
	GENERATED_BODY()

public:
	UGRTTileDataFactory();

	//--- UFactory Implementation ---//

	/**
	 * Creates the asset when the user clicks 'Create' in the menu.
	 */
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	/**
	 * Determines if this factory can create the new object from scratch.
	 */
	virtual bool ShouldShowInNewMenu() const override;
};
