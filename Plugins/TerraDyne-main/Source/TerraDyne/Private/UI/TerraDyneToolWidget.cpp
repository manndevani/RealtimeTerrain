// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/TerraDyneToolWidget.h"
#include "UI/STerraDynePanel.h"
#include "UI/TerraDyneFilePickerWidget.h"
#include "UI/TerraDyneWorldModeChoiceWidget.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "World/TerraDyneChunk.h"
#include "IO/TerraDyneArchiveHelper.h"
#include "Kismet/GameplayStatics.h"
#include "TerraDyneModule.h"

void UTerraDyneToolWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	// Initialize default values
	CurrentTool = ETerraDyneToolMode::SculptRaise;
	BrushRadius = 2000.0f;
	BrushStrength = 0.5f;
	BrushFalloff = 0.5f;
	
	UE_LOG(LogTemp, Log, TEXT("TerraDyneToolWidget constructed"));
}

TSharedRef<SWidget> UTerraDyneToolWidget::RebuildWidget()
{
	MainPanel = SNew(STerraDynePanel).ToolWidget(this);
	return MainPanel.ToSharedRef();
}

void UTerraDyneToolWidget::SaveWorld()
{
	if (UWorld* World = GetWorld())
	{
		if (UTerraDyneSubsystem* Subsystem = World->GetSubsystem<UTerraDyneSubsystem>())
		{
			if (ATerraDyneManager* Manager = Subsystem->GetTerrainManager())
			{
				UE_LOG(LogTerraDyne, Log, TEXT("UI: Saving world terrain state..."));
				Manager->SaveWorld();
			}
		}
	}
}

void UTerraDyneToolWidget::ResetTerrain()
{
	TArray<AActor*> Chunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), Chunks);

	for (AActor* Actor : Chunks)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			const int32 Num = Chunk->HeightBuffer.Num();
			if (Chunk->BaseBuffer.Num() != Num || Chunk->SculptBuffer.Num() != Num || Chunk->DetailBuffer.Num() != Num)
			{
				UE_LOG(LogTerraDyne, Warning, TEXT("ResetTerrain: Buffer size mismatch on chunk [%d,%d], skipping."),
					Chunk->GridCoordinate.X, Chunk->GridCoordinate.Y);
				continue;
			}
			for (int32 i = 0; i < Num; i++)
			{
				Chunk->BaseBuffer[i]   = 0.5f;
				Chunk->SculptBuffer[i] = 0.0f;
				Chunk->DetailBuffer[i] = 0.0f;
			}
			// Buildings are not terrain — flattening the world leaves their pads in place so the
			// structures standing on them keep their ground.
			Chunk->RecomputeHeightFromLayers();
			Chunk->RebuildPhysicsMesh();
		}
	}
	UE_LOG(LogTerraDyne, Warning, TEXT("UI: All Chunks Reset to Flat."));
}

void UTerraDyneToolWidget::ResetActiveLayer()
{
	UTerraDyneSubsystem* Subsystem = GetWorld()->GetSubsystem<UTerraDyneSubsystem>();
	if (!Subsystem) return;
	
	ATerraDyneManager* Manager = Subsystem->GetTerrainManager();
	if (!Manager) return;

	ETerraDyneLayer LayerToReset = Manager->ActiveLayer;

	if (LayerToReset == ETerraDyneLayer::Active)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("ResetActiveLayer: ETerraDyneLayer::Active is a selection marker; no data to reset."));
		return;
	}

	TArray<AActor*> Chunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), Chunks);

	for (AActor* Actor : Chunks)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			int32 Num = Chunk->HeightBuffer.Num();
			if (LayerToReset == ETerraDyneLayer::Base)
			{
				for (int32 i = 0; i < Num; i++) Chunk->BaseBuffer[i] = 0.5f;
			}
			else if (LayerToReset == ETerraDyneLayer::Sculpt)
			{
				for (int32 i = 0; i < Num; i++) Chunk->SculptBuffer[i] = 0.0f;
			}
			else if (LayerToReset == ETerraDyneLayer::Detail)
			{
				for (int32 i = 0; i < Num; i++) Chunk->DetailBuffer[i] = 0.0f;
			}

			Chunk->RecomputeHeightFromLayers();

			Chunk->RebuildPhysicsMesh();
			if (!Chunk->IsUsingGPU())
			{
				Chunk->UploadCPUHeightTexture();
			}
		}
	}
	UE_LOG(LogTerraDyne, Warning, TEXT("UI: Active Layer Reset."));
}

void UTerraDyneToolWidget::OpenGeoTIFFImportPicker()
{
	OpenRasterLayerImportPicker(ETerraDyneRasterLayer::Elevation);
}

void UTerraDyneToolWidget::OpenRasterLayerImportPicker(ETerraDyneRasterLayer Layer)
{
	PendingImportLayer = Layer;

	if (ActiveGeoTIFFPicker)
	{
		// Already up -- avoid stacking a second picker on top of it.
		return;
	}

	ActiveGeoTIFFPicker = CreateWidget<UTerraDyneFilePickerWidget>(GetOwningPlayer(), UTerraDyneFilePickerWidget::StaticClass());
	if (!ActiveGeoTIFFPicker)
	{
		return;
	}

	ActiveGeoTIFFPicker->OnFileChosen.AddDynamic(this, &UTerraDyneToolWidget::HandleGeoTIFFChosen);
	ActiveGeoTIFFPicker->OnCancelled.AddDynamic(this, &UTerraDyneToolWidget::HandleGeoTIFFPickerCancelled);
	ActiveGeoTIFFPicker->AddToViewport(100);
}

void UTerraDyneToolWidget::OpenOSMImportPicker()
{
	if (ActiveGeoTIFFPicker)
	{
		// Already up -- avoid stacking a second picker on top of it.
		return;
	}

	ActiveGeoTIFFPicker = CreateWidget<UTerraDyneFilePickerWidget>(GetOwningPlayer(), UTerraDyneFilePickerWidget::StaticClass());
	if (!ActiveGeoTIFFPicker)
	{
		return;
	}

	bPickerIsForOSM = true;
	ActiveGeoTIFFPicker->AllowedExtensions = { TEXT("osm"), TEXT("xml") };
	ActiveGeoTIFFPicker->TitleOverride = NSLOCTEXT("TerraDyneUI", "OSMPickerTitle", "SELECT AN OPENSTREETMAP EXPORT  (.OSM)");
	ActiveGeoTIFFPicker->OnFileChosen.AddDynamic(this, &UTerraDyneToolWidget::HandleOSMChosen);
	ActiveGeoTIFFPicker->OnCancelled.AddDynamic(this, &UTerraDyneToolWidget::HandleGeoTIFFPickerCancelled);
	ActiveGeoTIFFPicker->AddToViewport(100);
}

void UTerraDyneToolWidget::HandleOSMChosen(const FString& FilePath)
{
	if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(GetOwningPlayer()))
	{
		PC->RequestImportOSM(FilePath, bOSMImportBuildings, bOSMImportRoads);
	}

	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
	}
	bPickerIsForOSM = false;
}

void UTerraDyneToolWidget::HandleGeoTIFFChosen(const FString& FilePath)
{
	FString ResolvedPath;
	FString ExtractError;
	if (FTerraDyneArchiveHelper::ResolveOrExtractRaster(FilePath, PendingImportLayer, ResolvedPath, ExtractError))
	{
		if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(GetOwningPlayer()))
		{
			PC->RequestImportRasterLayer(PendingImportLayer, ResolvedPath);
		}
	}
	else
	{
		UE_LOG(LogTerraDyne, Error, TEXT("TerraDyne: Failed to resolve raster '%s': %s"), *FilePath, *ExtractError);
	}

	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
	}

	// The import request is away; hand the tools back so the player can work the imported world.
	RevealToolPanelAfterChoice();
}

void UTerraDyneToolWidget::HandleGeoTIFFPickerCancelled()
{
	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
	}

	// An OSM import is an ordinary in-session action, not the startup world-mode decision, so
	// cancelling it must not drag the player back to the Sandbox/Import choice.
	if (bPickerIsForOSM)
	{
		bPickerIsForOSM = false;
		RevealToolPanelAfterChoice();
		return;
	}

	// Cancelling the picker used to be a dead end: the choice widget was already gone, so the player
	// was left in a deliberately-empty world with no route back to either mode. Re-offer the choice
	// if nothing has actually started yet; otherwise just hand the tools back.
	ATerraDyneManager* Manager = GetManager();
	if (Manager && Manager->bAwaitingWorldModeChoice)
	{
		ShowWorldModeChoiceIfNeeded();
	}
	else
	{
		RevealToolPanelAfterChoice();
	}
}

ATerraDyneManager* UTerraDyneToolWidget::GetManager() const
{
	UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetTerrainManager() : nullptr;
}

void UTerraDyneToolWidget::ShowWorldModeChoiceIfNeeded()
{
	if (ActiveWorldModeChoice)
	{
		return;
	}

	ActiveWorldModeChoice = CreateWidget<UTerraDyneWorldModeChoiceWidget>(GetOwningPlayer(), UTerraDyneWorldModeChoiceWidget::StaticClass());
	if (!ActiveWorldModeChoice)
	{
		return;
	}

	ActiveWorldModeChoice->OnSandboxChosen.AddDynamic(this, &UTerraDyneToolWidget::HandleWorldModeSandboxChosen);
	ActiveWorldModeChoice->OnImportChosen.AddDynamic(this, &UTerraDyneToolWidget::HandleWorldModeImportChosen);
	ActiveWorldModeChoice->AddToViewport(200);

	SetVisibility(ESlateVisibility::Collapsed);
}

void UTerraDyneToolWidget::RevealToolPanelAfterChoice()
{
	SetVisibility(ESlateVisibility::Visible);
}

void UTerraDyneToolWidget::HandleWorldModeSandboxChosen()
{
	if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(GetOwningPlayer()))
	{
		PC->RequestStartSandboxMode();
	}
	else if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->StartSandboxMode();
	}

	if (ActiveWorldModeChoice)
	{
		ActiveWorldModeChoice->RemoveFromParent();
		ActiveWorldModeChoice = nullptr;
	}

	RevealToolPanelAfterChoice();
}

void UTerraDyneToolWidget::HandleWorldModeImportChosen()
{
	if (ATerraDyneManager* Manager = GetManager())
	{
		Manager->StartDEMImportMode();
	}

	if (ActiveWorldModeChoice)
	{
		ActiveWorldModeChoice->RemoveFromParent();
		ActiveWorldModeChoice = nullptr;
	}

	RevealToolPanelAfterChoice();
}
