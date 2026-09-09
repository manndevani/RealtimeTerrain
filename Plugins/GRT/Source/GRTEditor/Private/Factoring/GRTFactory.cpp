// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Factoring/GRTFactory.h"
#include "World/GRTTileData.h"

UGRTTileDataFactory::UGRTTileDataFactory()
{
	// Tell the Editor which class this factory creates
	SupportedClass = UGRTTileData::StaticClass();
	
	// Does it create a new object from scratch? Yes.
	bCreateNew = true;
	
	// Can it edit the object after creation? Yes.
	bEditAfterNew = true;
}

UObject* UGRTTileDataFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	// Create the asset
	UGRTTileData* NewAsset = NewObject<UGRTTileData>(InParent, InClass, InName, Flags);
	return NewAsset;
}

bool UGRTTileDataFactory::ShouldShowInNewMenu() const
{
	return true;
}
