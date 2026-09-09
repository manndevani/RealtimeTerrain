// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/GRTEditController.h"
#include "RealtimeRoadEditController.generated.h"

/**
 * Game-module player controller inheriting full road and terrain editing functionality
 * directly from the GRT plugin base controller.
 */
UCLASS()
class REALTIMETERRAIN_API ARealtimeRoadEditController : public AGRTEditController
{
	GENERATED_BODY()

public:
	ARealtimeRoadEditController() = default;
};
