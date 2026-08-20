// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Native Slate implementation of the startup Sandbox/Import choice. See
 * UTerraDyneWorldModeChoiceWidget for why this exists.
 */
class STerraDyneWorldModeChoicePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STerraDyneWorldModeChoicePanel) {}
		SLATE_EVENT(FSimpleDelegate, OnSandboxChosen)
		SLATE_EVENT(FSimpleDelegate, OnImportChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FSimpleDelegate OnSandboxChosenDelegate;
	FSimpleDelegate OnImportChosenDelegate;

	FReply OnSandboxClicked();
	FReply OnImportClicked();
};
