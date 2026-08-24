// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneManager.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Async/TaskGraphInterfaces.h"

void UTerraDyneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cesium's own world-origin rebasing (keeping the camera near small Unreal-space coordinates as
	// it moves real-world distances from wherever the level's default origin sits) shifts every
	// actor's absolute Z along with it. AWorldSettings::KillZ is a fixed absolute threshold, so a
	// rebase can make an airborne pawn spuriously read as "fallen below KillZ" and get destroyed --
	// which respawns it at the default PlayerStart, looking exactly like the camera "bouncing back"
	// mid-flight to wherever it started. This is not project-specific: CesiumRuntime itself logs
	// "Please consider disabling [world bounds checks] to avoid potential issues" whenever a
	// Cesium3DTileset loads with it still on. Forced off here, in world-subsystem Initialize (which
	// runs before any actor's BeginPlay, including whatever placed Cesium actor would otherwise log
	// that warning first), rather than relying on the level's saved WorldSettings, so it can't
	// silently regress if a new map is added or the checkbox gets re-enabled by accident.
	if (AWorldSettings* Settings = GetWorld()->GetWorldSettings())
	{
		Settings->bEnableWorldBoundsChecks = false;
	}

	UE_LOG(LogTemp, Log, TEXT("TerraDyneSubsystem: Initializing for World %s"), *GetWorld()->GetName());
}

void UTerraDyneSubsystem::Deinitialize()
{
	// Ensure we don't leave lingering threads or GC references
	FlushPendingTasks();

	ActiveManager.Reset();

	UE_LOG(LogTemp, Log, TEXT("TerraDyneSubsystem: Deinitialized."));

	Super::Deinitialize();
}

//--- Manager Registry ---//

void UTerraDyneSubsystem::RegisterManager(ATerraDyneManager* InManager)
{
	if (!InManager) return;

	if (ActiveManager.IsValid() && ActiveManager.Get() != InManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyneSubsystem: Multiple Managers detected! Overwriting registration to %s"), *InManager->GetName());
	}

	ActiveManager = InManager;
}

void UTerraDyneSubsystem::UnregisterManager(ATerraDyneManager* InManager)
{
	if (ActiveManager.Get() == InManager)
	{
		ActiveManager.Reset();
	}
}

ATerraDyneManager* UTerraDyneSubsystem::GetTerrainManager() const
{
	if (ActiveManager.IsValid())
	{
		return ActiveManager.Get();
	}
	return nullptr;
}

//--- IO Safety ---//

void UTerraDyneSubsystem::FlushPendingTasks()
{
	// Wait for IO (Thread Pool)
	// If we have critical save games writing to disk, we generally don't want to kill the process.
	// FNonAbandonableTasks running on the thread pool usually finish on their own, 
	// but explicit synchronization can be done here if we tracked specific IO tasks.
	
	// In a production environment, you might use a specific FGraphEvent to track active loads/saves
	// and wait for them:
	// FTaskGraphInterface::Get().WaitUntilTaskCompletes(MyTrackingHandle);
}
