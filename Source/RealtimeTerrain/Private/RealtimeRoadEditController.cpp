// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeRoadEditController.h"

#include "RealtimeRoadBuilderComponent.h"
#include "UI/TerraDyneToolWidget.h"

ARealtimeRoadEditController::ARealtimeRoadEditController()
{
	// A default subobject rather than a runtime spawn: the builder issues Server RPCs, which only
	// reach the server from a component that exists on an actor the player owns. A player
	// controller is exactly that, and creating it here means it is registered before any input
	// arrives.
	RoadBuilder = CreateDefaultSubobject<URealtimeRoadBuilderComponent>(TEXT("RoadBuilder"));
}

void ARealtimeRoadEditController::HandleRoadClick()
{
	const UTerraDyneToolWidget* ToolUI = GetToolWidget();
	if (!ToolUI || !RoadBuilder)
	{
		Super::HandleRoadClick();
		return;
	}

	if (ToolUI->CurrentTool == ETerraDyneToolMode::PlaceJunction)
	{
		FHitResult Hit;
		if (TraceUnderCursor(Hit))
		{
			RoadBuilder->SpawnJunctionAtLocation(Hit.Location, ERoadJunctionType::Cross_4Way);
		}
		return;
	}

	// The tool decides which kind of road a fresh one becomes.
	RoadBuilder->DefaultRoadType = (ToolUI->CurrentTool == ETerraDyneToolMode::DrawPavedRoad)
		? ERoadType::PavedRoad
		: ERoadType::RoughRoad;

	// A press that lands on a control point handle starts a drag and consumes the click; the
	// builder has already resolved what is under the cursor during its tick.
	if (RoadBuilder->BeginPointDrag())
	{
		return;
	}

	FHitResult Hit;
	if (TraceUnderCursor(Hit))
	{
		RoadBuilder->HandleWorldClick(Hit.Location);
	}
}

void ARealtimeRoadEditController::HandleRoadRelease()
{
	if (RoadBuilder)
	{
		RoadBuilder->EndPointDrag();
	}
}
