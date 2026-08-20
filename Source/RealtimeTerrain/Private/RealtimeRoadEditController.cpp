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

	// Select mode never draws. A press either grabs the handle under the cursor to drag it, or
	// picks whatever road was clicked so its handles come up.
	if (ToolUI->CurrentTool == ETerraDyneToolMode::SelectRoad)
	{
		if (RoadBuilder->BeginPointDrag())
		{
			return;
		}

		FHitResult Hit;
		if (TraceUnderCursor(Hit))
		{
			RoadBuilder->SelectRoadNear(Hit.Location);
		}
		return;
	}

	// The tool decides which kind of road a fresh one becomes.
	RoadBuilder->DefaultRoadType = (ToolUI->CurrentTool == ETerraDyneToolMode::DrawPavedRoad)
		? ERoadType::PavedRoad
		: ERoadType::RoughRoad;

	// Even while drawing, a press that lands on a handle drags it rather than extending the road.
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

bool ARealtimeRoadEditController::TryRoadUndo()
{
	return RoadBuilder && RoadBuilder->UndoRoadEdit();
}

bool ARealtimeRoadEditController::TryRoadRedo()
{
	return RoadBuilder && RoadBuilder->RedoRoadEdit();
}

void ARealtimeRoadEditController::DeleteSelectedRoadPoint()
{
	if (RoadBuilder)
	{
		RoadBuilder->DeleteSelectedPoint();
	}
}

bool ARealtimeRoadEditController::HasRoadPointSelected() const
{
	return RoadBuilder && RoadBuilder->HasPointSelected();
}

void ARealtimeRoadEditController::ClearRoadSelection()
{
	if (RoadBuilder)
	{
		RoadBuilder->ClearSelection();
	}
}

void ARealtimeRoadEditController::HandleRoadRelease()
{
	if (RoadBuilder)
	{
		RoadBuilder->EndPointDrag();
	}
}
