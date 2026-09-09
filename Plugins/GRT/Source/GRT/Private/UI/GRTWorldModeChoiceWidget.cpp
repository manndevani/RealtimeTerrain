// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/GRTWorldModeChoiceWidget.h"
#include "SGRTWorldModeChoicePanel.h"

TSharedRef<SWidget> UGRTWorldModeChoiceWidget::RebuildWidget()
{
	SAssignNew(Panel, SGRTWorldModeChoicePanel)
		.OnSandboxChosen(FSimpleDelegate::CreateUObject(this, &UGRTWorldModeChoiceWidget::HandlePanelSandboxChosen))
		.OnImportChosen(FSimpleDelegate::CreateUObject(this, &UGRTWorldModeChoiceWidget::HandlePanelImportChosen));

	return Panel.ToSharedRef();
}

void UGRTWorldModeChoiceWidget::HandlePanelSandboxChosen()
{
	OnSandboxChosen.Broadcast();
}

void UGRTWorldModeChoiceWidget::HandlePanelImportChosen()
{
	OnImportChosen.Broadcast();
}
