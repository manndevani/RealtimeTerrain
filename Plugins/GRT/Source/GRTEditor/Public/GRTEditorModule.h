// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Custom Log Category for Editor/Baking operations
GRTEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogGRTEditor, Log, All);

class FGRTEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Accessor for the module instance.
	 */
	static inline FGRTEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FGRTEditorModule>("GRTEditor");
	}

	/**
	 * Check if the module is loaded.
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("GRTEditor");
	}
};
