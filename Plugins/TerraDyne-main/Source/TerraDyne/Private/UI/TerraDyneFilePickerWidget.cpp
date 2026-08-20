// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/TerraDyneFilePickerWidget.h"
#include "STerraDyneFilePickerPanel.h"

TSharedRef<SWidget> UTerraDyneFilePickerWidget::RebuildWidget()
{
	SAssignNew(Panel, STerraDyneFilePickerPanel)
		.StartingDirectory(StartingDirectory)
		.OnFileChosen(FTerraDyneFilePickerChosen::CreateUObject(this, &UTerraDyneFilePickerWidget::HandlePanelFileChosen))
		.OnCancelled(FSimpleDelegate::CreateUObject(this, &UTerraDyneFilePickerWidget::HandlePanelCancelled));

	return Panel.ToSharedRef();
}

void UTerraDyneFilePickerWidget::HandlePanelFileChosen(FString FullPath)
{
	OnFileChosen.Broadcast(FullPath);
}

void UTerraDyneFilePickerWidget::HandlePanelCancelled()
{
	OnCancelled.Broadcast();
}
