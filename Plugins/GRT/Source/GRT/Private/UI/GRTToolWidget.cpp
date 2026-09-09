// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/GRTToolWidget.h"
#include "UI/SGRTPanel.h"
#include "UI/GRTFilePickerWidget.h"
#include "UI/GRTWorldModeChoiceWidget.h"
#include "Core/GRTSubsystem.h"
#include "Core/GRTManager.h"
#include "Core/GRTEditController.h"
#include "World/GRTChunk.h"
#include "IO/GRTArchiveHelper.h"
#include "Kismet/GameplayStatics.h"
#include "GRTModule.h"

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#endif

void UGRTToolWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	// Initialize default values
	CurrentTool = EGRTToolMode::SculptRaise;
	BrushRadius = 2000.0f;
	BrushStrength = 0.5f;
	BrushFalloff = 0.5f;
	
	UE_LOG(LogTemp, Log, TEXT("GRTToolWidget constructed"));
}

TSharedRef<SWidget> UGRTToolWidget::RebuildWidget()
{
	MainPanel = SNew(SGRTPanel).ToolWidget(this);
	return MainPanel.ToSharedRef();
}

void UGRTToolWidget::SaveWorld()
{
	if (UWorld* World = GetWorld())
	{
		if (UGRTSubsystem* Subsystem = World->GetSubsystem<UGRTSubsystem>())
		{
			if (AGRTManager* Manager = Subsystem->GetTerrainManager())
			{
				UE_LOG(LogGRT, Log, TEXT("UI: Saving world terrain state..."));
				Manager->SaveWorld();
			}
		}
	}
}

void UGRTToolWidget::ResetTerrain()
{
	TArray<AActor*> Chunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGRTChunk::StaticClass(), Chunks);

	for (AActor* Actor : Chunks)
	{
		if (AGRTChunk* Chunk = Cast<AGRTChunk>(Actor))
		{
			const int32 Num = Chunk->HeightBuffer.Num();
			if (Chunk->BaseBuffer.Num() != Num || Chunk->SculptBuffer.Num() != Num || Chunk->DetailBuffer.Num() != Num)
			{
				UE_LOG(LogGRT, Warning, TEXT("ResetTerrain: Buffer size mismatch on chunk [%d,%d], skipping."),
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
	UE_LOG(LogGRT, Warning, TEXT("UI: All Chunks Reset to Flat."));
}

void UGRTToolWidget::ResetActiveLayer()
{
	UGRTSubsystem* Subsystem = GetWorld()->GetSubsystem<UGRTSubsystem>();
	if (!Subsystem) return;
	
	AGRTManager* Manager = Subsystem->GetTerrainManager();
	if (!Manager) return;

	EGRTLayer LayerToReset = Manager->ActiveLayer;

	if (LayerToReset == EGRTLayer::Active)
	{
		UE_LOG(LogGRT, Warning, TEXT("ResetActiveLayer: EGRTLayer::Active is a selection marker; no data to reset."));
		return;
	}

	TArray<AActor*> Chunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGRTChunk::StaticClass(), Chunks);

	for (AActor* Actor : Chunks)
	{
		if (AGRTChunk* Chunk = Cast<AGRTChunk>(Actor))
		{
			int32 Num = Chunk->HeightBuffer.Num();
			if (LayerToReset == EGRTLayer::Base)
			{
				for (int32 i = 0; i < Num; i++) Chunk->BaseBuffer[i] = 0.5f;
			}
			else if (LayerToReset == EGRTLayer::Sculpt)
			{
				for (int32 i = 0; i < Num; i++) Chunk->SculptBuffer[i] = 0.0f;
			}
			else if (LayerToReset == EGRTLayer::Detail)
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
	UE_LOG(LogGRT, Warning, TEXT("UI: Active Layer Reset."));
}

void UGRTToolWidget::OpenGeoTIFFImportPicker()
{
	OpenRasterLayerImportPicker(EGRTRasterLayer::Elevation);
}

void UGRTToolWidget::OpenRasterLayerImportPicker(EGRTRasterLayer Layer)
{
	PendingImportLayer = Layer;

#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		const FString Title = FString::Printf(TEXT("Import %s"), *GRTRasterLayers::GetDisplayName(Layer).ToString());
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			Title,
			TEXT(""),
			TEXT(""),
			TEXT("GeoTIFF & Raster Archives (*.tif;*.tiff;*.tar.gz;*.tgz;*.zip)|*.tif;*.tiff;*.tar.gz;*.tgz;*.zip|GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|Archives (*.tar.gz;*.tgz;*.zip)|*.tar.gz;*.tgz;*.zip|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			HandleGeoTIFFChosen(OutFiles[0]);
		}
		else
		{
			// A bare return here used to skip HandleGeoTIFFPickerCancelled's cleanup entirely for
			// this path -- if the manager was still awaiting the startup Sandbox/Import choice,
			// cancelling the native dialog left the player stuck with no route back into either
			// mode, exactly the dead end that handler's own comment says it exists to prevent.
			HandleGeoTIFFPickerCancelled();
		}
		return;
	}
#endif

	if (ActiveGeoTIFFPicker)
	{
		// Already up -- avoid stacking a second picker on top of it.
		return;
	}

	ActiveGeoTIFFPicker = CreateWidget<UGRTFilePickerWidget>(GetOwningPlayer(), UGRTFilePickerWidget::StaticClass());
	if (!ActiveGeoTIFFPicker)
	{
		return;
	}

	ActiveGeoTIFFPicker->OnFileChosen.AddDynamic(this, &UGRTToolWidget::HandleGeoTIFFChosen);
	ActiveGeoTIFFPicker->OnCancelled.AddDynamic(this, &UGRTToolWidget::HandleGeoTIFFPickerCancelled);
	ActiveGeoTIFFPicker->AddToViewport(100);
}

void UGRTToolWidget::OpenOSMImportPicker()
{
#if WITH_EDITOR
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		// Set before the (blocking) dialog call, not after: HandleGeoTIFFPickerCancelled branches
		// on this to know an OSM import (not the startup world-mode choice) was in flight, and it
		// has to be true by the time a cancel is handled below.
		bPickerIsForOSM = true;

		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import OpenStreetMap Export"),
			TEXT(""),
			TEXT(""),
			TEXT("OpenStreetMap Data (*.osm;*.xml)|*.osm;*.xml|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles);

		if (bPicked && OutFiles.Num() > 0)
		{
			HandleOSMChosen(OutFiles[0]); // resets bPickerIsForOSM itself
		}
		else
		{
			// Same dead-end this fixes for the GeoTIFF path above: a bare return skipped
			// HandleGeoTIFFPickerCancelled's cleanup entirely for the native-dialog path.
			HandleGeoTIFFPickerCancelled();
		}
		return;
	}
#endif

	if (ActiveGeoTIFFPicker)
	{
		// Already up -- avoid stacking a second picker on top of it.
		return;
	}

	ActiveGeoTIFFPicker = CreateWidget<UGRTFilePickerWidget>(GetOwningPlayer(), UGRTFilePickerWidget::StaticClass());
	if (!ActiveGeoTIFFPicker)
	{
		return;
	}

	bPickerIsForOSM = true;
	ActiveGeoTIFFPicker->AllowedExtensions = { TEXT("osm"), TEXT("xml") };
	ActiveGeoTIFFPicker->TitleOverride = NSLOCTEXT("GRTUI", "OSMPickerTitle", "SELECT AN OPENSTREETMAP EXPORT  (.OSM)");
	ActiveGeoTIFFPicker->OnFileChosen.AddDynamic(this, &UGRTToolWidget::HandleOSMChosen);
	ActiveGeoTIFFPicker->OnCancelled.AddDynamic(this, &UGRTToolWidget::HandleGeoTIFFPickerCancelled);
	ActiveGeoTIFFPicker->AddToViewport(100);
}

void UGRTToolWidget::HandleOSMChosen(const FString& FilePath)
{
	if (AGRTEditController* PC = Cast<AGRTEditController>(GetOwningPlayer()))
	{
		PC->RequestImportOSM(FilePath, bOSMImportBuildings);
	}

	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
	}
	bPickerIsForOSM = false;
}

void UGRTToolWidget::HandleGeoTIFFChosen(const FString& FilePath)
{
	FString ResolvedPath;
	FString ExtractError;
	if (FGRTArchiveHelper::ResolveOrExtractRaster(FilePath, PendingImportLayer, ResolvedPath, ExtractError))
	{
		if (AGRTEditController* PC = Cast<AGRTEditController>(GetOwningPlayer()))
		{
			PC->RequestImportRasterLayer(PendingImportLayer, ResolvedPath);
		}
	}
	else
	{
		UE_LOG(LogGRT, Error, TEXT("GRT: Failed to resolve raster '%s': %s"), *FilePath, *ExtractError);
	}

	if (ActiveGeoTIFFPicker)
	{
		ActiveGeoTIFFPicker->RemoveFromParent();
		ActiveGeoTIFFPicker = nullptr;
	}

	// The import request is away; hand the tools back so the player can work the imported world.
	RevealToolPanelAfterChoice();
}

void UGRTToolWidget::HandleGeoTIFFPickerCancelled()
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
	AGRTManager* Manager = GetManager();
	if (Manager && Manager->bAwaitingWorldModeChoice)
	{
		ShowWorldModeChoiceIfNeeded();
	}
	else
	{
		RevealToolPanelAfterChoice();
	}
}

AGRTManager* UGRTToolWidget::GetManager() const
{
	UGRTSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UGRTSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetTerrainManager() : nullptr;
}

void UGRTToolWidget::ShowWorldModeChoiceIfNeeded()
{
	if (ActiveWorldModeChoice)
	{
		return;
	}

	ActiveWorldModeChoice = CreateWidget<UGRTWorldModeChoiceWidget>(GetOwningPlayer(), UGRTWorldModeChoiceWidget::StaticClass());
	if (!ActiveWorldModeChoice)
	{
		return;
	}

	ActiveWorldModeChoice->OnSandboxChosen.AddDynamic(this, &UGRTToolWidget::HandleWorldModeSandboxChosen);
	ActiveWorldModeChoice->OnImportChosen.AddDynamic(this, &UGRTToolWidget::HandleWorldModeImportChosen);
	ActiveWorldModeChoice->AddToViewport(200);

	SetVisibility(ESlateVisibility::Collapsed);
}

void UGRTToolWidget::RevealToolPanelAfterChoice()
{
	SetVisibility(ESlateVisibility::Visible);
}

void UGRTToolWidget::HandleWorldModeSandboxChosen()
{
	if (AGRTEditController* PC = Cast<AGRTEditController>(GetOwningPlayer()))
	{
		PC->RequestStartSandboxMode();
	}
	else if (AGRTManager* Manager = GetManager())
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

void UGRTToolWidget::HandleWorldModeImportChosen()
{
	if (AGRTManager* Manager = GetManager())
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
