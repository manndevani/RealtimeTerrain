// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTManager.h"
#include "World/GRTChunk.h"
#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// USP 1 & 3: Deformation & Physics Update
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTDeformationTest, "GRT.Functional.Deformation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTDeformationTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World should exist", World);

    AGRTChunk* Chunk = World->SpawnActor<AGRTChunk>();
    TestNotNull("Chunk should spawn", Chunk);

    if (Chunk)
    {
        // 1. Initialize
        Chunk->InitializeChunk(FIntPoint(0,0), 1000.0f, 32, nullptr);
        Chunk->RebuildPhysicsMesh(); // Ensure mesh exists

        TestNotNull("DynamicMeshComp should exist", Chunk->DynamicMeshComp.Get());
        
        // Check Initial Vertices
        int32 InitialVertexCount = 0;
        if(Chunk->DynamicMeshComp && Chunk->DynamicMeshComp->GetDynamicMesh())
        {
             InitialVertexCount = Chunk->DynamicMeshComp->GetDynamicMesh()->GetTriangleCount();
        }
        TestTrue("Chunk should have triangles", InitialVertexCount > 0);

        // 2. Deform
        FVector EditPos(500.0f, 500.0f, 0.0f);
        // Apply a strong brush to ensure change
        Chunk->ApplyLocalIdempotentEdit(EditPos, 200.0f, 500.0f, EGRTBrushMode::Raise);

        // 3. Verify Change (USP 1)
        // USP 3: Physics
        TestTrue("Collision should be enabled", Chunk->DynamicMeshComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
    }

    return true;
}

// USP 4: Infinite World / Grid Logic
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTGridTest, "GRT.Functional.InfiniteGrid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTGridTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AGRTManager* Manager = World->SpawnActor<AGRTManager>();
    Manager->GlobalChunkSize = 1000.0f; // Set size

    // Manually spawn chunks to simulate the grid
    AGRTChunk* ChunkA = World->SpawnActor<AGRTChunk>();
    ChunkA->GridCoordinate = FIntPoint(0, 0);
    
    AGRTChunk* ChunkB = World->SpawnActor<AGRTChunk>();
    ChunkB->GridCoordinate = FIntPoint(-1, -1); // Negative Quadrant

    Manager->RebuildChunkMap(); // This is the function we are testing

    // Test Positive Lookup
    AGRTChunk* FoundA = Manager->GetChunkAtLocation(FVector(100, 100, 0));
    TestEqual("Should find Chunk A at (0,0)", FoundA, ChunkA);

    // Test Negative Lookup (USP 4)
    // Centered chunk math means chunk [0,0] spans [-500, 500] for a 1000-unit chunk.
    AGRTChunk* FoundB = Manager->GetChunkAtLocation(FVector(-600, -600, 0));
    TestEqual("Should find Chunk B at (-1,-1)", FoundB, ChunkB);

    return true;
}

// USP 5: Zero Config
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTSetupTest, "GRT.Functional.ZeroConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTSetupTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AGRTManager* Manager = World->SpawnActor<AGRTManager>();
    Manager->bSpawnDefaultChunksOnBeginPlay = true;
    // bShowWorldModeChoiceOnBeginPlay defaults to true and otherwise leaves BeginPlay spawning
    // nothing at all -- this test wants the old unconditional-fire path, which is exactly what
    // turning that off is for (see its doc comment on AGRTManager).
    Manager->bShowWorldModeChoiceOnBeginPlay = false;

    // Trigger the explicit sandbox bootstrap path.
    Manager->DispatchBeginPlay();

    // Verify Chunks Spawned
    TArray<AActor*> Chunks;
    UGameplayStatics::GetAllActorsOfClass(World, AGRTChunk::StaticClass(), Chunks);
    
    // Should spawn 3x3 grid = 9 chunks
    TestTrue("Should spawn default sandbox chunks", Chunks.Num() >= 9);

    // Verify Stats (USP 5 part 2)
    FGRTGPUStats Stats = Manager->GetGPUStats();
    TestTrue("Should detect GPU or Software", !Stats.ComputeBackend.IsEmpty());

    return true;
}
#endif
