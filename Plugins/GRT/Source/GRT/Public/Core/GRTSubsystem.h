// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GRTSubsystem.generated.h"

// Forward Declarations
class AGRTManager;

/**
 * UGRTSubsystem
 *
 * The central lifecycle registry for the GRT plugin within a specific World.
 * Functions:
 * 1. Caches the active Terrain Manager for global O(1) access.
 * 2. Tracks background IO tasks to ensure data safety on World Teardown.
 */
UCLASS()
class GRT_API UGRTSubsystem : public UWorldSubsystem
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
	 * called by AGRTManager::BeginPlay() to register itself. 
	 * Validates only one manager exists per world.
	 */
	void RegisterManager(AGRTManager* InManager);

	/** Called by AGRTManager::EndPlay() */
	void UnregisterManager(AGRTManager* InManager);

	/** 
	 * Returns the currently active manager.
	 * Returns nullptr if the world has no GRT terrain.
	 */
	UFUNCTION(BlueprintPure, Category = "GRT")
	AGRTManager* GetTerrainManager() const;

	//--- IO Safety ---//

	/** 
	 * Waits for all pending Async Save tasks to complete.
	 * Crucial to call before level transitions to prevent data corruption.
	 */
	UFUNCTION(BlueprintCallable, Category = "GRT|System")
	void FlushPendingTasks();

private:
	// Weak pointer avoids keeping the actor alive if the level is unloaded violently
	UPROPERTY(Transient)
	TWeakObjectPtr<AGRTManager> ActiveManager;
};
