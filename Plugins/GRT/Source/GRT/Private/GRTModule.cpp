// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "GRTModule.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h" // For mapping shader directories if you add custom shaders later
#include "Misc/Paths.h"

// Define the log category
DEFINE_LOG_CATEGORY(LogGRT);

#define LOCTEXT_NAMESPACE "FGRTModule"

void FGRTModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; 
	// the exact timing is specified in the .uplugin file per-module by LoadingPhase.
	
	UE_LOG(LogGRT, Log, TEXT("GRT Runtime Module Started."));

	// OPTIONAL: If Phase 4 evolves to use Custom Shaders (.usf), 
	// this is where you map the virtual directory.
	/*
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("GRT"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/GRT"), PluginShaderDir);
	*/
}

void FGRTModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module. 
	// For modules that support dynamic reloading, we call this function before unloading the module.
	
	UE_LOG(LogGRT, Log, TEXT("GRT Runtime Module Shutting Down."));
}

#undef LOCTEXT_NAMESPACE
	
// Implement the module. 
// "GRT" must match the name in your .uplugin and .Build.cs
IMPLEMENT_MODULE(FGRTModule, GRT)
