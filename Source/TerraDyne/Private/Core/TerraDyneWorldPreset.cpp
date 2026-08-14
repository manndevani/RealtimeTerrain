// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneWorldPreset.h"

UTerraDyneWorldPreset::UTerraDyneWorldPreset()
{
	PresetId = TEXT("DefaultRuntimeFramework");
	DisplayName = FText::FromString(TEXT("Default Runtime Framework"));
	Description = FText::FromString(TEXT("Baseline TerraDyne preset for persistent authored and procedural worlds."));
}
