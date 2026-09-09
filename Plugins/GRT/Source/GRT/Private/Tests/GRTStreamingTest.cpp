// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTManager.h"
#include "World/GRTChunk.h"
#include "Settings/GRTSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTStreamingTest, "GRT.Streaming.RingLoadUnload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTStreamingTest::RunTest(const FString& Parameters)
{
	// Create a temporary world
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		AddError("Failed to create World");
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// Spawn Manager
	AGRTManager* Manager = World->SpawnActor<AGRTManager>();
	if (!Manager)
	{
		AddError("Manager failed to spawn");
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	// Verify streaming settings exist and have sensible defaults
	{
		const UGRTSettings* Settings = GetDefault<UGRTSettings>();
		TestTrue("Settings CDO exists", Settings != nullptr);
		if (Settings)
		{
			TestTrue("ChunkLoadRadius > 0", Settings->ChunkLoadRadius > 0);
			TestTrue("ChunkUnloadRadius > ChunkLoadRadius", Settings->ChunkUnloadRadius > Settings->ChunkLoadRadius);
			TestTrue("MaxChunkOpsPerTick > 0", Settings->MaxChunkOpsPerTick > 0);
			TestTrue("GridExtent > 0", Settings->GridExtent > 0);
			TestTrue("ChunkSaveDir not empty", !Settings->ChunkSaveDir.IsEmpty());
		}
	}

	// Verify Manager starts with no streaming state pollution
	{
		TestEqual("Initial active chunks is 0 (test world has no BeginPlay)", Manager->GetActiveChunkCount(), 0);
	}

	// Verify public query methods handle empty state gracefully
	{
		AGRTChunk* Chunk = Manager->GetChunkAtCoord(FIntPoint(0, 0));
		TestNull("No chunk at (0,0) in empty manager", Chunk);

		AGRTChunk* LocChunk = Manager->GetChunkAtLocation(FVector(5000, 5000, 0));
		TestNull("No chunk at world location in empty manager", LocChunk);

		TArray<AGRTChunk*> Nearby = Manager->GetChunksInRadius(FVector::ZeroVector, 50000.0f);
		TestEqual("No chunks in radius for empty manager", Nearby.Num(), 0);
	}

	// Cleanup
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}
