// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/GRTEditGameMode.h"
#include "RealtimeTerrainEditGameMode.generated.h"

/**
 * The plugin's edit game mode with the road-aware player controller swapped in.
 *
 * Everything else — pawn class, the spread-out spawn handling for co-located players — comes from
 * the base. The only reason this exists is that PlayerControllerClass has to point at a controller
 * in this module, since that is the only place the road classes are visible.
 */
UCLASS()
class REALTIMETERRAIN_API ARealtimeTerrainEditGameMode : public AGRTEditGameMode
{
	GENERATED_BODY()

public:
	ARealtimeTerrainEditGameMode();
};
