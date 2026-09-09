// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/GRTManager.h"
#include "Modules/ModuleManager.h"
#include "Building/GRTGizmoActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTLoadTest, "GRT.Basic.ModuleLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTLoadTest::RunTest(const FString& Parameters)
{
    // Test 1: Check if Module is Loaded
    bool bIsLoaded = FModuleManager::Get().IsModuleLoaded("GRT");
    TestTrue("GRT module should be loaded", bIsLoaded);

    // Test 2: Check Class Validity
    UClass* ManagerClass = AGRTManager::StaticClass();
    TestNotNull("AGRTManager class should be valid", ManagerClass);

    if (ManagerClass)
    {
        AGRTManager* DefaultObject = Cast<AGRTManager>(ManagerClass->GetDefaultObject());
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
#include "World/GRTChunk.h"
#include "World/GRTOrchestrator.h"
#include "FileHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTLevelOverlapAuditTest, "GRT.Basic.LevelOverlapAudit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTLevelOverlapAuditTest::RunTest(const FString& Parameters)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/GRT/Maps/NewWorld"));
    TestNotNull("Loaded /GRT/Maps/NewWorld map", World);
    if (!World)
    {
        return false;
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    UE_LOG(LogTemp, Display, TEXT("=== AUDIT: Total actors in /GRT/Maps/NewWorld = %d ==="), AllActors.Num());

    TArray<AActor*> LandscapeActors;
    TArray<AActor*> ManagerActors;
    TArray<AActor*> OrchestratorActors;
    TArray<AGRTChunk*> ChunkActors;

    for (AActor* Actor : AllActors)
    {
        if (!Actor) continue;
        FString ClassName = Actor->GetClass()->GetName();
        if (ClassName.Contains(TEXT("Landscape")))
        {
            LandscapeActors.Add(Actor);
            UE_LOG(LogTemp, Display, TEXT("AUDIT LANDSCAPE: %s (%s) at %s"), *Actor->GetName(), *ClassName, *Actor->GetActorLocation().ToString());
        }
        else if (Actor->IsA<AGRTManager>())
        {
            ManagerActors.Add(Actor);
            UE_LOG(LogTemp, Display, TEXT("AUDIT MANAGER: %s at %s"), *Actor->GetName(), *Actor->GetActorLocation().ToString());
        }
        else if (Actor->IsA<AGRTOrchestrator>())
        {
            OrchestratorActors.Add(Actor);
            UE_LOG(LogTemp, Display, TEXT("AUDIT ORCHESTRATOR: %s at %s"), *Actor->GetName(), *Actor->GetActorLocation().ToString());
        }
        else if (AGRTChunk* Chunk = Cast<AGRTChunk>(Actor))
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
#include "World/GRTChunk.h"
#include "World/GRTOrchestrator.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTChunk0AndChunk1CheckTest, "GRT.Basic.Chunk0AndChunk1Check", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTChunk0AndChunk1CheckTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World created", World);
    if (!World) return false;

    AGRTManager* Manager = World->SpawnActor<AGRTManager>();
    TestNotNull("Manager spawned", Manager);
    if (!Manager) return false;

    Manager->GlobalChunkSize = 12800.0f;

    AGRTChunk* Chunk0 = World->SpawnActor<AGRTChunk>(AGRTChunk::StaticClass(), FTransform(FVector(0.0f, 0.0f, 0.0f)));
    if (Chunk0)
    {
        Chunk0->GridCoordinate = FIntPoint(0, 0);
        Chunk0->Initialize(128, 12800.0f);
        Manager->RegisterChunk(Chunk0);
    }

    AGRTChunk* Chunk1 = World->SpawnActor<AGRTChunk>(AGRTChunk::StaticClass(), FTransform(FVector(12800.0f, 0.0f, 0.0f)));
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

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGRTGizmoRuntimeAssetsTest,
    "GRT.Building.GizmoRuntimeAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * AGRTGizmoActor is a RUNTIME visual (building footprint spline-point handles -- its own
 * class comment explains why the name no longer matches its scope), so it has to hold up outside
 * the editor. Two things about it were wrong and neither showed up in the editor, which is why
 * they are pinned here. (Its former axis/translate-gizmo responsibility moved to the new
 * ARuntimeGizmoActor/UGizmoManipulationSubsystem system -- see GRT.Gizmo.* tests for that.)
 */
bool FGRTGizmoRuntimeAssetsTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World exists"), World);
    if (!World)
    {
        return false;
    }

    AGRTGizmoActor* GizmoA = World->SpawnActor<AGRTGizmoActor>();
    AGRTGizmoActor* GizmoB = World->SpawnActor<AGRTGizmoActor>();
    TestNotNull(TEXT("First gizmo spawned"), GizmoA);
    TestNotNull(TEXT("Second gizmo spawned"), GizmoB);
    if (!GizmoA || !GizmoB)
    {
        return false;
    }

    // Populate one handle on each so there is something to check -- the array starts empty.
    const TArray<FVector> OnePoint = { FVector::ZeroVector };
    GizmoA->UpdateSplinePointHandles(OnePoint, FTransform::Identity);
    GizmoB->UpdateSplinePointHandles(OnePoint, FTransform::Identity);
    TestEqual(TEXT("First gizmo has one handle"), GizmoA->SplinePointMeshHandles.Num(), 1);
    TestEqual(TEXT("Second gizmo has one handle"), GizmoB->SplinePointMeshHandles.Num(), 1);
    if (GizmoA->SplinePointMeshHandles.Num() != 1 || GizmoB->SplinePointMeshHandles.Num() != 1)
    {
        return false;
    }

    // Handle meshes must resolve. The spline-point sphere used to be held in a Transient property,
    // so it was never serialized into the CDO and never seen by the cooker.
    UStaticMeshComponent* HandleA = GizmoA->SplinePointMeshHandles[0];
    TestNotNull(TEXT("First handle component exists"), HandleA);
    if (!HandleA)
    {
        return false;
    }
    UStaticMesh* HandleMesh = HandleA->GetStaticMesh();
    TestNotNull(TEXT("Handle mesh resolved"), HandleMesh);

    UMaterialInterface* HandleMat = HandleA->GetMaterial(0);
    TestNotNull(TEXT("Handle has a material"), HandleMat);
    if (!HandleMat)
    {
        return false;
    }

    // The base material must come from a tree that survives cooking. /Engine/EditorMaterials and
    // /Engine/VREditor are editor-only and are simply absent from a packaged build.
    UMaterialInterface* BaseMat = HandleMat;
    if (UMaterialInstanceDynamic* AsMID = Cast<UMaterialInstanceDynamic>(HandleMat))
    {
        BaseMat = Cast<UMaterialInterface>(AsMID->Parent);
    }
    TestNotNull(TEXT("Resolved a base material behind the MID"), BaseMat);
    if (BaseMat)
    {
        const FString BasePath = BaseMat->GetPathName();
        TestFalse(
            FString::Printf(TEXT("Base material is not editor-only content (%s)"), *BasePath),
            BasePath.StartsWith(TEXT("/Engine/EditorMaterials")) || BasePath.StartsWith(TEXT("/Engine/VREditor")));
    }

    // Each gizmo needs its own MIDs. Built in the constructor they belonged to the CDO, so every
    // spawned gizmo shared one set and highlighting a handle on one recoloured all of them.
    UStaticMeshComponent* HandleB = GizmoB->SplinePointMeshHandles[0];
    UMaterialInterface* MatA = HandleA->GetMaterial(0);
    UMaterialInterface* MatB = HandleB ? HandleB->GetMaterial(0) : nullptr;
    TestNotNull(TEXT("Second gizmo has its own handle material"), MatB);
    TestNotEqual(TEXT("Two gizmos do not share one dynamic material instance"), MatA, MatB);

    return true;
}

#endif
