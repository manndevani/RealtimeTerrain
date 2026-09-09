// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "GRTEditorModule.h"
#include "Modules/ModuleManager.h"

// Define the Log Category declared in the header
DEFINE_LOG_CATEGORY(LogGRTEditor);

#define LOCTEXT_NAMESPACE "FGRTEditorModule"

void FGRTEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; 
	// the exact timing is specified in the .uplugin file per-module by LoadingPhase.
	
	UE_LOG(LogGRTEditor, Log, TEXT("GRT Editor Module Started."));

	// Phase 4 Note:
	// If you want to add custom "Context Menu" actions for UGRTTileData assets 
	// (like "Re-Bake" or "Inspection"), you would register FAssetTypeActions_Base here 
	// using the IAssetTools interface.
	
	/* Example Registration (Optional):
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_GRTTileData()));
	*/
}

void FGRTEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.
	
	UE_LOG(LogGRTEditor, Log, TEXT("GRT Editor Module Shutting Down."));
}

#undef LOCTEXT_NAMESPACE
	
// Implement the module logic
IMPLEMENT_MODULE(FGRTEditorModule, GRTEditor)
