// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeTerrainEditGameMode.h"

#include "RealtimeRoadEditController.h"

ARealtimeTerrainEditGameMode::ARealtimeTerrainEditGameMode()
{
	PlayerControllerClass = ARealtimeRoadEditController::StaticClass();
}
