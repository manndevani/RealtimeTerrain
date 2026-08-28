// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeRoadEditController.h"

#include "RealtimeRoadBuilderComponent.h"
#include "RealtimeRoadSplineActor.h"
#include "UI/TerraDyneToolWidget.h"
#include "Engine/World.h"

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

AActor* ARealtimeRoadEditController::CreateRoadFromOSM(const TArray<FVector>& WorldPoints, float WidthMeters)
{
	if (WorldPoints.Num() < 2)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// OSM points arrive on the georeference plane (height 0), because a .osm document carries no
	// elevation. A road drawn by hand gets its Z from the cursor trace against the terrain, so do
	// the same here -- otherwise every imported road would sit at the georeference origin's height
	// and float above or bury itself under the ground it is supposed to follow.
	TArray<FVector> Grounded;
	Grounded.Reserve(WorldPoints.Num());
	for (const FVector& Point : WorldPoints)
	{
		FVector Resolved = Point;

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSMRoadGroundSample), /*bTraceComplex*/ true);
		const FVector Start(Point.X, Point.Y, Point.Z + 500000.0);
		const FVector End(Point.X, Point.Y, Point.Z - 500000.0);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			Resolved.Z = Hit.Location.Z;
		}
		Grounded.Add(Resolved);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>(
		ARealtimeRoadSplineActor::StaticClass(), Grounded[0], FRotator::ZeroRotator, SpawnParams);
	if (!Road)
	{
		return nullptr;
	}

	Road->RoadType = ERoadType::PavedRoad;
	// OSM widths are metres; CrossSectionSettings.RoadWidth is centimetres, and drives the terrain
	// pad, junction half-width and crossing-merge radius as well as the swept section.
	Road->CrossSectionSettings.RoadWidth = FMath::Max(WidthMeters, 1.0f) * 100.0f;
	Road->SetPointsRealtime(Grounded);

	return Road;
}
