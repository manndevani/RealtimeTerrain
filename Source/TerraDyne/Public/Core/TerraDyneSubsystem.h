// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TerraDyneSubsystem.generated.h"

// Forward Declarations
class ATerraDyneManager;

/**
 * UTerraDyneSubsystem
 *
 * The central lifecycle registry for the TerraDyne plugin within a specific World.
 * Functions:
 * 1. Caches the active Terrain Manager for global O(1) access.
 * 2. Tracks background IO tasks to ensure data safety on World Teardown.
 */
UCLASS()
class TERRADYNE_API UTerraDyneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//--- Subsystem Lifecycle ---//
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	// Called by the engine when the world ticks (if we need global time slicing)
	// virtual void Tick(float DeltaTime) override; // Uncomment if inheriting FTickableGameObject

	//--- Manager Registry ---//

	/** 
	 * called by ATerraDyneManager::BeginPlay() to register itself. 
	 * Validates only one manager exists per world.
	 */
	void RegisterManager(ATerraDyneManager* InManager);

	/** Called by ATerraDyneManager::EndPlay() */
	void UnregisterManager(ATerraDyneManager* InManager);

	/** 
	 * Returns the currently active manager.
	 * Returns nullptr if the world has no TerraDyne terrain.
	 */
	UFUNCTION(BlueprintPure, Category = "TerraDyne")
	ATerraDyneManager* GetTerrainManager() const;

	//--- IO Safety ---//

	/** 
	 * Waits for all pending Async Save tasks to complete.
	 * Crucial to call before level transitions to prevent data corruption.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void FlushPendingTasks();

private:
	// Weak pointer avoids keeping the actor alive if the level is unloaded violently
	UPROPERTY(Transient)
	TWeakObjectPtr<ATerraDyneManager> ActiveManager;
};
