// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneLoadTest, "TerraDyne.Basic.ModuleLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneLoadTest::RunTest(const FString& Parameters)
{
    // Test 1: Check if Module is Loaded
    bool bIsLoaded = FModuleManager::Get().IsModuleLoaded("TerraDyne");
    TestTrue("TerraDyne module should be loaded", bIsLoaded);

    // Test 2: Check Class Validity
    UClass* ManagerClass = ATerraDyneManager::StaticClass();
    TestNotNull("ATerraDyneManager class should be valid", ManagerClass);

    if (ManagerClass)
    {
        ATerraDyneManager* DefaultObject = Cast<ATerraDyneManager>(ManagerClass->GetDefaultObject());
        TestNotNull("Default Object should exist", DefaultObject);
        
        // Test 3: Check Default Values
        TestTrue("Default GlobalChunkSize should be 0 or initialized", DefaultObject->GlobalChunkSize >= 0.0f);
        TestFalse("bAutoImportAtRuntime should be opt-in", DefaultObject->bAutoImportAtRuntime);
        TestFalse("Demo chunk bootstrap should be opt-in", DefaultObject->bSpawnDefaultChunksOnBeginPlay);
    }

    return true;
}

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "FileHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneLevelOverlapAuditTest, "TerraDyne.Basic.LevelOverlapAudit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneLevelOverlapAuditTest::RunTest(const FString& Parameters)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/NewWorld"));
    TestNotNull("Loaded /Game/NewWorld map", World);
    if (!World)
    {
        return false;
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    UE_LOG(LogTemp, Display, TEXT("=== AUDIT: Total actors in /Game/NewWorld = %d ==="), AllActors.Num());

    TArray<AActor*> LandscapeActors;
    TArray<AActor*> ManagerActors;
    TArray<AActor*> OrchestratorActors;
    TArray<ATerraDyneChunk*> ChunkActors;

    for (AActor* Actor : AllActors)
    {
        if (!Actor) continue;
        FString ClassName = Actor->GetClass()->GetName();
        if (ClassName.Contains(TEXT("Landscape")))
        {
            LandscapeActors.Add(Actor);
            UE_LOG(LogTemp, Display, TEXT("AUDIT LANDSCAPE: %s (%s) at %s"), *Actor->GetName(), *ClassName, *Actor->GetActorLocation().ToString());
        }
        else if (Actor->IsA<ATerraDyneManager>())
        {
            ManagerActors.Add(Actor);
            UE_LOG(LogTemp, Display, TEXT("AUDIT MANAGER: %s at %s"), *Actor->GetName(), *Actor->GetActorLocation().ToString());
        }
        else if (Actor->IsA<ATerraDyneOrchestrator>())
        {
            OrchestratorActors.Add(Actor);
            UE_LOG(LogTemp, Display, TEXT("AUDIT ORCHESTRATOR: %s at %s"), *Actor->GetName(), *Actor->GetActorLocation().ToString());
        }
        else if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
        {
            ChunkActors.Add(Chunk);
            UE_LOG(LogTemp, Display, TEXT("AUDIT CHUNK: %s (Coord [%d,%d]) at %s"), *Chunk->GetName(), Chunk->GridCoordinate.X, Chunk->GridCoordinate.Y, *Chunk->GetActorLocation().ToString());
        }
    }

    UE_LOG(LogTemp, Display, TEXT("AUDIT SUMMARY: Landscapes=%d, Managers=%d, Orchestrators=%d, Chunks=%d"),
        LandscapeActors.Num(), ManagerActors.Num(), OrchestratorActors.Num(), ChunkActors.Num());

    return true;
}

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneChunk0AndChunk1CheckTest, "TerraDyne.Basic.Chunk0AndChunk1Check", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneChunk0AndChunk1CheckTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World created", World);
    if (!World) return false;

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull("Manager spawned", Manager);
    if (!Manager) return false;

    Manager->GlobalChunkSize = 12800.0f;

    ATerraDyneChunk* Chunk0 = World->SpawnActor<ATerraDyneChunk>(ATerraDyneChunk::StaticClass(), FTransform(FVector(0.0f, 0.0f, 0.0f)));
    if (Chunk0)
    {
        Chunk0->GridCoordinate = FIntPoint(0, 0);
        Chunk0->Initialize(128, 12800.0f);
        Manager->RegisterChunk(Chunk0);
    }

    ATerraDyneChunk* Chunk1 = World->SpawnActor<ATerraDyneChunk>(ATerraDyneChunk::StaticClass(), FTransform(FVector(12800.0f, 0.0f, 0.0f)));
    if (Chunk1)
    {
        Chunk1->GridCoordinate = FIntPoint(1, 0);
        Chunk1->Initialize(128, 12800.0f);
        Manager->RegisterChunk(Chunk1);
    }

    TestNotNull("Chunk0 spawned", Chunk0);
    TestNotNull("Chunk1 spawned", Chunk1);
    if (!Chunk0 || !Chunk1) return false;

    FVector Loc0 = Chunk0->GetActorLocation();
    FVector Loc1 = Chunk1->GetActorLocation();
    float WS0 = Chunk0->WorldSize;
    float WS1 = Chunk1->WorldSize;

    UE_LOG(LogTemp, Display, TEXT("=== CHUNK 0 & CHUNK 1 GEOMETRY INSPECTION ==="));
    UE_LOG(LogTemp, Display, TEXT("Chunk 0 (Coord [0,0]): Location=%s, WorldSize=%.1f, Resolution=%d"), *Loc0.ToString(), WS0, Chunk0->Resolution);
    UE_LOG(LogTemp, Display, TEXT("Chunk 1 (Coord [1,0]): Location=%s, WorldSize=%.1f, Resolution=%d"), *Loc1.ToString(), WS1, Chunk1->Resolution);

    // Chunk 0 X extents: [Loc0.X - WS0/2, Loc0.X + WS0/2]
    float Chunk0_MinX = Loc0.X - WS0 * 0.5f;
    float Chunk0_MaxX = Loc0.X + WS0 * 0.5f;

    // Chunk 1 X extents: [Loc1.X - WS1/2, Loc1.X + WS1/2]
    float Chunk1_MinX = Loc1.X - WS1 * 0.5f;
    float Chunk1_MaxX = Loc1.X + WS1 * 0.5f;

    UE_LOG(LogTemp, Display, TEXT("Chunk 0 X range: [%.2f, %.2f]"), Chunk0_MinX, Chunk0_MaxX);
    UE_LOG(LogTemp, Display, TEXT("Chunk 1 X range: [%.2f, %.2f]"), Chunk1_MinX, Chunk1_MaxX);

    float OverlapDistance = Chunk0_MaxX - Chunk1_MinX;
    UE_LOG(LogTemp, Display, TEXT("Seam Boundary: Chunk0 MaxX = %.2f, Chunk1 MinX = %.2f (Delta = %.4f)"), Chunk0_MaxX, Chunk1_MinX, OverlapDistance);

    TestEqual("Seam boundary matches exactly (no gap, no area overlap)", OverlapDistance, 0.0f);

    return true;
}
