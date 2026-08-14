// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDynePanel.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneManager.h"
#include "World/TerraDyneOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#endif
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Colors/SColorBlock.h"

#define LOCTEXT_NAMESPACE "TerraDyneUI"

void STerraDynePanel::Construct(const FArguments& InArgs)
{
	OwnerWidget = InArgs._ToolWidget;

	// Define some simple styles locally
	FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	FSlateColor TitleColor = FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
	FSlateColor BGColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.9f);
	FSlateColor SectionBGColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.8f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>(this, &STerraDynePanel::GetWindowPadding))
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BGColor)
				.Padding(10.0f)
				[
					SNew(SVerticalBox)
					
					//--- HEADER (Draggable Area) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 10)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor::Transparent)
						.Padding(2.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Title", "TERRADYNE"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
								.ColorAndOpacity(TitleColor)
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(SSpacer)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Bottom)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Version", "v0.3"))
								.Font(LabelFont)
								.ColorAndOpacity(FLinearColor::Gray)
							]
						]
					]

					//--- LEVELS SECTION (file-based export/import) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("LevelsHeader", "LEVELS")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnSaveFile", "Save to File"))
									.OnClicked(this, &STerraDynePanel::SaveToFile)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("BtnLoadFile", "Load from File"))
									.OnClicked(this, &STerraDynePanel::LoadFromFile)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
							[
								SNew(STextBlock).Text_Lambda([this](){ return GetExportStatusText(); }).Font(LabelFont)
							]
						]
					]

					//--- SCULPTING SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("SculptHeader", "SCULPTING")).Font(HeaderFont)
							]
							
							// Tool Modes — Row 1: Raise / Lower / Smooth
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeRaise", "Raise"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptRaise); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptRaise)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeLower", "Lower"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptLower); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptLower)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeSmooth", "Smooth"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Smooth); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Smooth)
								.HAlign(HAlign_Center)
							]
						]
						// Tool Modes — Row 2: Flatten
						// Paint was removed along with its layer picker: the terrain material derives
						// its layer split from vertex colors and no longer samples a WeightMap, so
						// painting a layer had no visual effect.
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeFlatten", "Flatten"))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Flatten); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Flatten)
								.HAlign(HAlign_Center)
							]
						]

						// Dynamic Layers
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 5)
							[
								SNew(STextBlock).Text(LOCTEXT("LayerHeader", "DYNAMIC LAYERS")).Font(LabelFont).ColorAndOpacity(FLinearColor::Gray)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerBase", "Base"))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Base); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Base)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerSculpt", "Sculpt"))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Sculpt); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Sculpt)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerDetail", "Detail"))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Detail); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Detail)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnResetLayer", "Reset Active Layer"))
								.OnClicked(this, &STerraDynePanel::ResetActiveLayer)
								.HAlign(HAlign_Center)
							]

							// Sliders
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock).Text(LOCTEXT("LblRadius", "Radius: "))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock)
										.Text(this, &STerraDynePanel::GetBrushRadiusText)
										.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SSlider)
									.MinValue(100.0f)
									.MaxValue(10000.0f)
									.Value(this, &STerraDynePanel::GetBrushRadius)
									.OnValueChanged(this, &STerraDynePanel::OnBrushRadiusChanged)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock).Text(LOCTEXT("LblStrength", "Strength: "))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock)
										.Text(this, &STerraDynePanel::GetBrushStrengthText)
										.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SSlider)
									.MinValue(0.0f)
									.MaxValue(5.0f)
									.Value(this, &STerraDynePanel::GetBrushStrength)
									.OnValueChanged(this, &STerraDynePanel::OnBrushStrengthChanged)
								]
							]
						]
					]

					//--- TELEMETRY SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("StatsHeader", "TELEMETRY")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetStatsText)
								.Font(LabelFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetGPUStatsText)
								.ColorAndOpacity(this, &STerraDynePanel::GetGPUStatusColor)
								.Font(LabelFont)
							]
						]
					]
				]
			]
		]
	];
}

void STerraDynePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

FReply STerraDynePanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Simple hit test: if within top 50px relative to window position
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D RelativeMouse = LocalMouse - WindowPosition;

		if (RelativeMouse.X >= 0 && RelativeMouse.X <= 300 && RelativeMouse.Y >= 0 && RelativeMouse.Y <= 50)
		{
			bIsDragging = true;
			DragOffset = RelativeMouse;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D NewPos = LocalMouse - DragOffset;

		// Clamp so the panel stays within the viewport
		FVector2D ViewSize = MyGeometry.GetLocalSize();
		NewPos.X = FMath::Clamp(NewPos.X, 0.0f, FMath::Max(0.0f, ViewSize.X - 300.0f));
		NewPos.Y = FMath::Clamp(NewPos.Y, 0.0f, FMath::Max(0.0f, ViewSize.Y - 50.0f));
		WindowPosition = NewPos;

		return FReply::Handled();
	}
	return FReply::Unhandled();
}

//--- Helpers ---
ATerraDyneOrchestrator* STerraDynePanel::GetOrchestrator() const
{
	if (OwnerWidget.IsValid())
	{
		return Cast<ATerraDyneOrchestrator>(UGameplayStatics::GetActorOfClass(OwnerWidget->GetWorld(), ATerraDyneOrchestrator::StaticClass()));
	}
	return nullptr;
}

ATerraDyneManager* STerraDynePanel::GetManager() const
{
	if (OwnerWidget.IsValid())
	{
		if (UWorld* World = OwnerWidget->GetWorld())
		{
			// Assuming Manager is a singleton or accessible via Subsystem, but looking for Actor for now
			return Cast<ATerraDyneManager>(UGameplayStatics::GetActorOfClass(World, ATerraDyneManager::StaticClass()));
		}
	}
	return nullptr;
}

//--- Callbacks ---

FText STerraDynePanel::GetBrushRadiusText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("RadiusFmt", "{0}m"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushRadius / 100.f)));
	}
	return LOCTEXT("RadiusNA", "--");
}

FText STerraDynePanel::GetBrushStrengthText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("StrengthFmt", "{0}%"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushStrength * 20.0f)));
	}
	return LOCTEXT("StrengthNA", "--");
}

FReply STerraDynePanel::StartPersistenceTest()
{
	if (ATerraDyneOrchestrator* Orch = GetOrchestrator())
	{
		Orch->StartPersistenceTest();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::SaveToFile()
{
#if WITH_EDITOR
	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr)
	{
		return FReply::Unhandled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Unhandled();
	}

	const FString DefaultPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerraDyneExports")));
	IFileManager::Get().MakeDirectory(*DefaultPath, true);

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Save TerraDyne Level"),
		DefaultPath,
		TEXT("MyLevel.terradyne"),
		TEXT("TerraDyne Level (*.terradyne)|*.terradyne"),
		EFileDialogFlags::None,
		OutFiles);

	if (bPicked && OutFiles.Num() > 0)
	{
		LastExportPath = OutFiles[0];
		Mgr->SaveWorldToFile(LastExportPath);
	}
	return FReply::Handled();
#else
	return FReply::Unhandled();
#endif
}

FReply STerraDynePanel::LoadFromFile()
{
#if WITH_EDITOR
	ATerraDyneManager* Mgr = GetManager();
	if (!Mgr)
	{
		return FReply::Unhandled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Unhandled();
	}

	const FString DefaultPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerraDyneExports")));

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Load TerraDyne Level"),
		DefaultPath,
		TEXT(""),
		TEXT("TerraDyne Level (*.terradyne)|*.terradyne"),
		EFileDialogFlags::None,
		OutFiles);

	if (bPicked && OutFiles.Num() > 0)
	{
		LastExportPath = OutFiles[0];
		Mgr->LoadWorldFromFile(LastExportPath);
	}
	return FReply::Handled();
#else
	return FReply::Unhandled();
#endif
}

FText STerraDynePanel::GetExportStatusText() const
{
	return LastExportPath.IsEmpty()
		? LOCTEXT("ExportNone", "No file selected yet")
		: FText::Format(LOCTEXT("ExportLast", "Last: {0}"), FText::FromString(LastExportPath));
}

FReply STerraDynePanel::StartShowcase()
{
	ATerraDyneOrchestrator* Orch = GetOrchestrator();
	if (!Orch && OwnerWidget.IsValid() && OwnerWidget->GetWorld())
	{
		Orch = OwnerWidget->GetWorld()->SpawnActor<ATerraDyneOrchestrator>(
			ATerraDyneOrchestrator::StaticClass(),
			FVector(0.0f, 0.0f, 500.0f),
			FRotator::ZeroRotator);
	}

	if (Orch)
	{
		Orch->RunScriptedShowcase();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply STerraDynePanel::SetActiveLayer(ETerraDyneLayer NewLayer)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->ActiveLayer = NewLayer;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetLayerColor(ETerraDyneLayer Layer) const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->ActiveLayer == Layer)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
		}
	}
	return FLinearColor::Gray;
}

FReply STerraDynePanel::ResetActiveLayer()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ResetActiveLayer();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::SetToolMode(ETerraDyneToolMode NewMode)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->CurrentTool = NewMode;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetToolModeColor(ETerraDyneToolMode Mode) const
{
	if (OwnerWidget.IsValid())
	{
		if (OwnerWidget->CurrentTool == Mode)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
		}
	}
	return FLinearColor::Gray;
}

float STerraDynePanel::GetBrushRadius() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->BrushRadius : 2000.0f;
}

void STerraDynePanel::OnBrushRadiusChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushRadius = FMath::Clamp(NewValue, 100.0f, 10000.0f);
	}
}

float STerraDynePanel::GetBrushStrength() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->BrushStrength : 0.5f;
}

void STerraDynePanel::OnBrushStrengthChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushStrength = FMath::Clamp(NewValue, 0.0f, 5.0f);
	}
}

FText STerraDynePanel::GetStatsText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		return FText::Format(LOCTEXT("StatsFmt", "Chunks: {0} | Verts: {1}"), 
			FText::AsNumber(Mgr->GetActiveChunkCount()), 
			FText::AsNumber(Mgr->GetTotalVertexCount()));
	}
	return LOCTEXT("StatsNA", "Terrain Manager Not Found");
}

FText STerraDynePanel::GetGPUStatsText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		FTerraDyneGPUStats Stats = Mgr->GetGPUStats();
		return FText::Format(LOCTEXT("GPUStatsFmt", "{0}\n{1}"), 
			FText::FromString(Stats.ComputeBackend),
			FText::FromString(Stats.AdapterName));
	}
	return FText::GetEmpty();
}

FSlateColor STerraDynePanel::GetGPUStatusColor() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->GetGPUStats().bIsCudaAvailable)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // Blue for hybrid RT-assisted path
		}
	}
	return FLinearColor::Gray;
}

bool STerraDynePanel::IsUndoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			return Mgr->CanUndo(PC);
		}
	}
	return false;
}

bool STerraDynePanel::IsRedoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			return Mgr->CanRedo(PC);
		}
	}
	return false;
}

FReply STerraDynePanel::OnUndoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	// Straight to the manager rather than through a player-controller cast. This used to require
	// ATerraDyneEditController, which is not the controller the game actually spawns — so the cast
	// always failed and the button silently did nothing.
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			Mgr->Undo(PC);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnRedoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			Mgr->Redo(PC);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
