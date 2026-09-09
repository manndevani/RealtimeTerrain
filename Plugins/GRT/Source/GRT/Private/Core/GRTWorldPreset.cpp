// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/GRTWorldPreset.h"

UGRTWorldPreset::UGRTWorldPreset()
{
	PresetId = TEXT("DefaultRuntimeFramework");
	DisplayName = FText::FromString(TEXT("Default Runtime Framework"));
	Description = FText::FromString(TEXT("Baseline GRT preset for persistent authored and procedural worlds."));
}
