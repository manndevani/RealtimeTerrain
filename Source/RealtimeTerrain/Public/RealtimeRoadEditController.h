// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneEditController.h"
#include "RealtimeRoadEditController.generated.h"

class URealtimeRoadBuilderComponent;

/**
 * Player controller that drives the road tools directly.
 *
 * The base controller in the TerraDyne plugin can only reach road actors through reflection —
 * StaticLoadClass by path, FindFunction, ProcessEvent — because a plugin cannot depend on the game
 * module that owns them. That means it also cannot see URealtimeRoadBuilderComponent, so it ended
 * up reimplementing a thinner version of it with its own selection state.
 *
 * This subclass lives in the game module, which can reference both, so it overrides the road hooks
 * and forwards them to the builder component with ordinary typed calls. The builder becomes the
 * one place road editing logic lives.
 */
UCLASS()
class REALTIMETERRAIN_API ARealtimeRoadEditController : public ATerraDyneEditController
{
	GENERATED_BODY()

public:
	ARealtimeRoadEditController();

	/** Owns the road tool state: selection, the draft ghost, the edit overlay and drag handling. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road")
	TObjectPtr<URealtimeRoadBuilderComponent> RoadBuilder;

	virtual void HandleRoadClick() override;
	virtual void HandleRoadRelease() override;

	virtual bool TryRoadUndo() override;
	virtual bool TryRoadRedo() override;
	virtual void DeleteSelectedRoadPoint() override;
	virtual bool HasRoadPointSelected() const override;
	virtual void ClearRoadSelection() override;

	/**
	 * Builds one road from an OSM centreline.
	 *
	 * The plugin-side importer classifies the ways and converts them to world space but cannot
	 * construct a road itself -- ARealtimeRoadSplineActor lives in this module, which the plugin
	 * cannot reference -- so it calls out through this hook, exactly as it does for HandleRoadClick.
	 */
	virtual AActor* CreateRoadFromOSM(const TArray<FVector>& WorldPoints, float WidthMeters) override;
};
