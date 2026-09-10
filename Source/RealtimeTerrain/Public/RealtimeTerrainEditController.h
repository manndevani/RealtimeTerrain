// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/GRTEditController.h"
#include "RealtimeTerrainEditController.generated.h"

/**
 * Game-module player controller. Inherits all terrain, building, model, and OSM editing from the
 * GRT plugin's AGRTEditController; exists so the project has its own class to extend.
 */
UCLASS()
class REALTIMETERRAIN_API ARealtimeTerrainEditController : public AGRTEditController
{
	GENERATED_BODY()

public:
	ARealtimeTerrainEditController() = default;
};
