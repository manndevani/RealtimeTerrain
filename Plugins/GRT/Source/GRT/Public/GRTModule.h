// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Declare a custom log category for the plugin.
// Usage in code: UE_LOG(LogGRT, Warning, TEXT("Message"));
GRT_API DECLARE_LOG_CATEGORY_EXTERN(LogGRT, Log, All);

class FGRTModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Accessor for the module instance.
	 * Optional, but helpful for accessing singleton subsystems later.
	 */
	static inline FGRTModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FGRTModule>("GRT");
	}

	/**
	 * Check if the module is loaded.
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("GRT");
	}
};
