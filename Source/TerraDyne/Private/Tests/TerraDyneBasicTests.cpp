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
