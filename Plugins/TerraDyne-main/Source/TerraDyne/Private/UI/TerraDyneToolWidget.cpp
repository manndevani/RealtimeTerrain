// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/TerraDyneToolWidget.h"
#include "UI/STerraDynePanel.h"
#include "UI/TerraDyneFilePickerWidget.h"
#include "UI/TerraDyneWorldModeChoiceWidget.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "World/TerraDyneChunk.h"
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

void UTerraDyneToolWidget::HandleGeoTIFFChosen(const FString& FilePath)
{
	if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(GetOwningPlayer()))
	{
		PC->RequestImportGeoTIFF(FilePath);
	}

	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
	}
}

void UTerraDyneToolWidget::HandleGeoTIFFPickerCancelled()
{
	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
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

	ATerraDyneManager* Manager = GetManager();
	if (!Manager || !Manager->bShowWorldModeChoiceOnBeginPlay)
	{
		return;
	}

	// Only genuinely undecided -- not a reload of a level that already resolved to an import in an
	// earlier session, which BeginPlay resumes on its own regardless of this setting.
	if (Manager->WorldOriginKind != ETerraDyneChunkOrigin::Procedural)
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
	// Above the main panel (Z 100) and the GeoTIFF picker (also Z 100) -- this is the very first
	// thing the player should see and act on.
	ActiveWorldModeChoice->AddToViewport(200);
}

void UTerraDyneToolWidget::HandleWorldModeSandboxChosen()
{
	if (ATerraDyneEditController* PC = Cast<ATerraDyneEditController>(GetOwningPlayer()))
	{
		PC->RequestStartSandboxMode();
	}

	if (ActiveWorldModeChoice)
	{
		ActiveWorldModeChoice->RemoveFromParent();
		ActiveWorldModeChoice = nullptr;
	}
}

void UTerraDyneToolWidget::HandleWorldModeImportChosen()
{
	if (ActiveWorldModeChoice)
	{
		ActiveWorldModeChoice->RemoveFromParent();
		ActiveWorldModeChoice = nullptr;
	}

	// Reuses the exact same picker flow the tool panel's own "Import GeoTIFF..." button uses.
	OpenGeoTIFFImportPicker();
}
