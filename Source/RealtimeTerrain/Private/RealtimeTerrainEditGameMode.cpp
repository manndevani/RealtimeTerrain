// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeTerrainEditGameMode.h"

#include "RealtimeTerrainEditController.h"

ARealtimeTerrainEditGameMode::ARealtimeTerrainEditGameMode()
{
	PlayerControllerClass = ARealtimeTerrainEditController::StaticClass();
}
