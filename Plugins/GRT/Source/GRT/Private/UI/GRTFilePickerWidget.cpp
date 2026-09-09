// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/GRTFilePickerWidget.h"
#include "SGRTFilePickerPanel.h"

TSharedRef<SWidget> UGRTFilePickerWidget::RebuildWidget()
{
	SAssignNew(Panel, SGRTFilePickerPanel)
		.StartingDirectory(StartingDirectory)
		.AllowedExtensions(AllowedExtensions)
		.TitleOverride(TitleOverride)
		.OnFileChosen(FGRTFilePickerChosen::CreateUObject(this, &UGRTFilePickerWidget::HandlePanelFileChosen))
		.OnCancelled(FSimpleDelegate::CreateUObject(this, &UGRTFilePickerWidget::HandlePanelCancelled));

	return Panel.ToSharedRef();
}

void UGRTFilePickerWidget::HandlePanelFileChosen(FString FullPath)
{
	OnFileChosen.Broadcast(FullPath);
}

void UGRTFilePickerWidget::HandlePanelCancelled()
{
	OnCancelled.Broadcast();
}
