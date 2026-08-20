// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/TerraDyneWorldModeChoiceWidget.h"
#include "STerraDyneWorldModeChoicePanel.h"

TSharedRef<SWidget> UTerraDyneWorldModeChoiceWidget::RebuildWidget()
{
	SAssignNew(Panel, STerraDyneWorldModeChoicePanel)
		.OnSandboxChosen(FSimpleDelegate::CreateUObject(this, &UTerraDyneWorldModeChoiceWidget::HandlePanelSandboxChosen))
		.OnImportChosen(FSimpleDelegate::CreateUObject(this, &UTerraDyneWorldModeChoiceWidget::HandlePanelImportChosen));

	return Panel.ToSharedRef();
}

void UTerraDyneWorldModeChoiceWidget::HandlePanelSandboxChosen()
{
	OnSandboxChosen.Broadcast();
}

void UTerraDyneWorldModeChoiceWidget::HandlePanelImportChosen()
{
	OnImportChosen.Broadcast();
}
