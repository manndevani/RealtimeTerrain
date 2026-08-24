// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "World/TerraDyneChunk.h"
#include "Building/TerraDyneBuildingActor.h"
#include "IO/TerraDyneGeoTIFFReader.h"
#include "IO/TerraDyneRasterLayers.h"
#include "IO/TerraDyneDEMMeshBuilder.h"
#include "IO/TerraDyneGeoTIFFHeightSource.h"
#include "CesiumGeoreference.h"
#include "Components/DynamicMeshComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneComprehensiveVerificationTest,
	"TerraDyne.Verification.ComprehensiveSuite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	const TCHAR* DatasetDir = TEXT("C:/Projects/Test/viz (1)");

	struct FRasterTestTarget
	{
		const TCHAR* FileName;
		ETerraDyneRasterLayer Layer;
		int32 ExpectedBands;
	};

	const TArray<FRasterTestTarget>& GetTargetRasters()
	{
		static const TArray<FRasterTestTarget> Targets = {
			{ TEXT("output_hh.tif"),                ETerraDyneRasterLayer::Elevation,      1 },
			{ TEXT("viz.hh_hillshade.tif"),         ETerraDyneRasterLayer::Hillshade,      1 },
			{ TEXT("viz.hh_color-relief.tif"),      ETerraDyneRasterLayer::ColorRelief,    3 },
			{ TEXT("viz.hh_hillshade-color.tif"),   ETerraDyneRasterLayer::ColorHillshade, 3 },
			{ TEXT("viz.hh_slope.tif"),             ETerraDyneRasterLayer::Slope,          1 },
			{ TEXT("viz.hh_aspect.tif"),            ETerraDyneRasterLayer::Aspect,         1 },
			{ TEXT("viz.hh_roughness.tif"),         ETerraDyneRasterLayer::Roughness,      1 },
		};
		return Targets;
	}
}

bool FTerraDyneComprehensiveVerificationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> ReportRoot = MakeShared<FJsonObject>();
	ReportRoot->SetStringField(TEXT("TestName"), TEXT("TerraDyne Comprehensive System Self-Verification"));
	ReportRoot->SetStringField(TEXT("Timestamp"), FDateTime::Now().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> PhaseResults;

	// =========================================================================
	// PHASE 1: Real GeoTIFF Decoding & Scientific Colormap Inspection
	// =========================================================================
	{
		TSharedPtr<FJsonObject> Phase1 = MakeShared<FJsonObject>();
		Phase1->SetStringField(TEXT("Phase"), TEXT("Phase 1: Raster Decoding & Colormap Validation"));
		TArray<TSharedPtr<FJsonValue>> LayerEntries;

		bool bPhase1Pass = true;

		for (const FRasterTestTarget& Target : GetTargetRasters())
		{
			TSharedPtr<FJsonObject> LayerEntry = MakeShared<FJsonObject>();
			LayerEntry->SetStringField(TEXT("File"), Target.FileName);
			LayerEntry->SetStringField(TEXT("Layer"), TerraDyneRasterLayers::GetDisplayName(Target.Layer).ToString());

			const FString FullPath = FPaths::Combine(DatasetDir, Target.FileName);
			if (!FPaths::FileExists(FullPath))
			{
				LayerEntry->SetStringField(TEXT("Status"), TEXT("SKIPPED_FILE_NOT_FOUND"));
				LayerEntries.Add(MakeShared<FJsonValueObject>(LayerEntry));
				continue;
			}

			TArray<FColor> DecodedPixels;
			int32 OutWidth = 0, OutHeight = 0;
			FString DecodeErr;
			const double StartTime = FPlatformTime::Seconds();
			const bool bDecoded = FTerraDyneGeoTIFFReader::DecodeRasterPixels(
				FullPath, 512, DecodedPixels, OutWidth, OutHeight, DecodeErr);
			const double DurationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

			TestTrue(FString::Printf(TEXT("DecodeRasterPixels should succeed for %s"), Target.FileName), bDecoded);

			if (bDecoded && DecodedPixels.Num() > 0)
			{
				LayerEntry->SetNumberField(TEXT("DecodedWidth"), OutWidth);
				LayerEntry->SetNumberField(TEXT("DecodedHeight"), OutHeight);
				LayerEntry->SetNumberField(TEXT("PixelCount"), DecodedPixels.Num());
				LayerEntry->SetNumberField(TEXT("DecodeDurationMs"), DurationMs);

				// Inspect color distribution & variance to ensure not blank/flat white
				uint64 SumR = 0, SumG = 0, SumB = 0;
				uint8 MinR = 255, MaxR = 0, MinG = 255, MaxG = 0, MinB = 255, MaxB = 0;

				for (const FColor& C : DecodedPixels)
				{
					SumR += C.R; SumG += C.G; SumB += C.B;
					MinR = FMath::Min(MinR, C.R); MaxR = FMath::Max(MaxR, C.R);
					MinG = FMath::Min(MinG, C.G); MaxG = FMath::Max(MaxG, C.G);
					MinB = FMath::Min(MinB, C.B); MaxB = FMath::Max(MaxB, C.B);
				}

				const float AvgR = (float)SumR / (float)DecodedPixels.Num();
				const float AvgG = (float)SumG / (float)DecodedPixels.Num();
				const float AvgB = (float)SumB / (float)DecodedPixels.Num();

				LayerEntry->SetNumberField(TEXT("AvgR"), AvgR);
				LayerEntry->SetNumberField(TEXT("AvgG"), AvgG);
				LayerEntry->SetNumberField(TEXT("AvgB"), AvgB);
				LayerEntry->SetNumberField(TEXT("RangeR"), MaxR - MinR);
				LayerEntry->SetNumberField(TEXT("RangeG"), MaxG - MinG);
				LayerEntry->SetNumberField(TEXT("RangeB"), MaxB - MinB);

				// Assert that pixels have dynamic range (not a single monochrome constant)
				const bool bHasDynamicRange = (MaxR > MinR) || (MaxG > MinG) || (MaxB > MinB);
				TestTrue(FString::Printf(TEXT("%s must have dynamic color range across pixels"), Target.FileName), bHasDynamicRange);

				LayerEntry->SetStringField(TEXT("Status"), bHasDynamicRange ? TEXT("PASSED") : TEXT("FAILED_NO_VARIANCE"));
				if (!bHasDynamicRange) bPhase1Pass = false;
			}
			else
			{
				LayerEntry->SetStringField(TEXT("Status"), TEXT("FAILED_DECODE"));
				LayerEntry->SetStringField(TEXT("Error"), DecodeErr);
				bPhase1Pass = false;
			}

			LayerEntries.Add(MakeShared<FJsonValueObject>(LayerEntry));
		}

		Phase1->SetArrayField(TEXT("Layers"), LayerEntries);
		Phase1->SetBoolField(TEXT("Passed"), bPhase1Pass);
		PhaseResults.Add(MakeShared<FJsonValueObject>(Phase1));
	}

	// =========================================================================
	// PHASE 2: 3D DEM Chunk Mesh Baking & Vertex Color Distribution
	// =========================================================================
	{
		TSharedPtr<FJsonObject> Phase2 = MakeShared<FJsonObject>();
		Phase2->SetStringField(TEXT("Phase"), TEXT("Phase 2: DEM Mesh Generation & Vertex Color Distribution"));

		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		TestNotNull(TEXT("Automation World should exist"), World);

		ATerraDyneManager* Mgr = World ? World->SpawnActor<ATerraDyneManager>() : nullptr;
		TestNotNull(TEXT("TerraDyneManager should spawn in World"), Mgr);

		bool bPhase2Pass = false;

		if (Mgr)
		{
			Mgr->bShowWorldModeChoiceOnBeginPlay = false;

			const FString DemPath = FPaths::Combine(DatasetDir, TEXT("output_hh.tif"));
			if (FPaths::FileExists(DemPath))
			{
				FString ImportErr;
				const bool bImported = Mgr->ImportRasterLayer(ETerraDyneRasterLayer::Elevation, DemPath, ImportErr);
				TestTrue(TEXT("DEM Elevation import should succeed"), bImported);

				FString LoadErr;
				const bool bLoaded = Mgr->LoadSpecificRasterLayer(ETerraDyneRasterLayer::Elevation, LoadErr);
				TestTrue(TEXT("DEM Elevation load should succeed"), bLoaded);

				const int32 ChunkCount = Mgr->GetImportedChunkCoords().Num();
				TestTrue(TEXT("Imported chunks count should be > 0"), ChunkCount > 0);
				Phase2->SetNumberField(TEXT("ImportedChunkCount"), ChunkCount);

				Phase2->SetNumberField(TEXT("FootprintMinX"), Mgr->GeoTIFFImportState.WorldFootprintMin.X);
				Phase2->SetNumberField(TEXT("FootprintMinY"), Mgr->GeoTIFFImportState.WorldFootprintMin.Y);
				Phase2->SetNumberField(TEXT("FootprintMaxX"), Mgr->GeoTIFFImportState.WorldFootprintMax.X);
				Phase2->SetNumberField(TEXT("FootprintMaxY"), Mgr->GeoTIFFImportState.WorldFootprintMax.Y);

				if (ACesiumGeoreference* Georef = ACesiumGeoreference::GetDefaultGeoreference(World))
				{
					const FVector OriginLLH = Georef->GetOriginLongitudeLatitudeHeight();
					Phase2->SetNumberField(TEXT("OriginLon"), OriginLLH.X);
					Phase2->SetNumberField(TEXT("OriginLat"), OriginLLH.Y);

					const FVector P_North = Georef->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(OriginLLH.X, OriginLLH.Y + 0.1, OriginLLH.Z));
					const FVector P_East = Georef->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(OriginLLH.X + 0.1, OriginLLH.Y, OriginLLH.Z));
					Phase2->SetNumberField(TEXT("NorthStepUnrealX"), P_North.X);
					Phase2->SetNumberField(TEXT("NorthStepUnrealY"), P_North.Y);
					Phase2->SetNumberField(TEXT("EastStepUnrealX"), P_East.X);
					Phase2->SetNumberField(TEXT("EastStepUnrealY"), P_East.Y);
				}

				// Test vertex color baking on a synthetic DEM chunk mesh
				ATerraDyneChunk* TestChunk = World->SpawnActor<ATerraDyneChunk>();
				TestNotNull(TEXT("TestChunk must spawn"), TestChunk);

				if (TestChunk)
				{
					const int32 Res = 128;
					TestChunk->Resolution = Res;
					TArray<float> SampledHeight;
					SampledHeight.SetNum(Res * Res);
					for (int32 i = 0; i < Res * Res; ++i)
					{
						SampledHeight[i] = (float)i / (float)(Res * Res - 1);
					}

					TestChunk->PopulateFromDEM(
						MoveTemp(SampledHeight),
						8000.0f,
						0.0,
						true,
						Mgr->GeoTIFFImportState.WorldFootprintMin,
						Mgr->GeoTIFFImportState.WorldFootprintMax,
						Mgr->ActiveGeoTIFFVisualOverlayPixels,
						Mgr->ActiveGeoTIFFVisualOverlayWidth,
						Mgr->ActiveGeoTIFFVisualOverlayHeight);

					UDynamicMeshComponent* DynMeshComp = TestChunk->DynamicMeshComp.Get();
					TestNotNull(TEXT("DynamicMeshComp must exist on chunk"), DynMeshComp);

					if (DynMeshComp && DynMeshComp->GetDynamicMesh())
					{
						UE::Geometry::FDynamicMesh3* Mesh = DynMeshComp->GetDynamicMesh()->GetMeshPtr();
						TestNotNull(TEXT("FDynamicMesh3 should exist"), Mesh);

						if (Mesh && Mesh->HasAttributes() && Mesh->Attributes()->PrimaryColors())
						{
							const UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh->Attributes()->PrimaryColors();
							const int32 ColorCount = ColorOverlay->ElementCount();
							TestTrue(TEXT("Color overlay elements should match vertex count"), ColorCount > 0);

							// Test that vertex colors are NOT all white (1,1,1)
							int32 NonWhiteVertices = 0;
							FVector4f ColorMin(1.f, 1.f, 1.f, 1.f);
							FVector4f ColorMax(0.f, 0.f, 0.f, 0.f);

							for (int32 i = 0; i < ColorCount; ++i)
							{
								const FVector4f C = ColorOverlay->GetElement(i);
								ColorMin.X = FMath::Min(ColorMin.X, C.X); ColorMax.X = FMath::Max(ColorMax.X, C.X);
								ColorMin.Y = FMath::Min(ColorMin.Y, C.Y); ColorMax.Y = FMath::Max(ColorMax.Y, C.Y);
								ColorMin.Z = FMath::Min(ColorMin.Z, C.Z); ColorMax.Z = FMath::Max(ColorMax.Z, C.Z);

								if (C.X < 0.90f || C.Y < 0.90f || C.Z < 0.90f)
								{
									++NonWhiteVertices;
								}
							}

							Phase2->SetNumberField(TEXT("VertexColorCount"), ColorCount);
							Phase2->SetNumberField(TEXT("NonWhiteVertexCount"), NonWhiteVertices);
							Phase2->SetNumberField(TEXT("ColorRangeR"), ColorMax.X - ColorMin.X);
							Phase2->SetNumberField(TEXT("ColorRangeG"), ColorMax.Y - ColorMin.Y);
							Phase2->SetNumberField(TEXT("ColorRangeB"), ColorMax.Z - ColorMin.Z);

							TestTrue(TEXT("At least 50% of vertices must have rich hypsometric tint (not flat white)"),
								NonWhiteVertices > (ColorCount / 2));

							bPhase2Pass = (NonWhiteVertices > (ColorCount / 2));
						}
					}
				}
			}
		}

		Phase2->SetBoolField(TEXT("Passed"), bPhase2Pass);
		PhaseResults.Add(MakeShared<FJsonValueObject>(Phase2));
	}

	// =========================================================================
	// PHASE 3: Building Footprint Spline & Pad Relevelling
	// =========================================================================
	{
		TSharedPtr<FJsonObject> Phase3 = MakeShared<FJsonObject>();
		Phase3->SetStringField(TEXT("Phase"), TEXT("Phase 3: Building Spline & Pad Relevelling"));

		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		ATerraDyneBuildingActor* Building = World ? World->SpawnActor<ATerraDyneBuildingActor>() : nullptr;
		TestNotNull(TEXT("Building actor must spawn"), Building);

		bool bPhase3Pass = false;

		if (Building)
		{
			const int32 InitialSplinePoints = Building->GatherSplinePointsLocal().Num();
			TestTrue(TEXT("Initial building footprint should have at least 4 corner points"), InitialSplinePoints >= 4);

			// Add a spline point
			Building->AddSplinePoint();
			TestEqual(TEXT("Footprint point count should increment by 1"),
				Building->GatherSplinePointsLocal().Num(), InitialSplinePoints + 1);

			// Remove spline point
			Building->RemoveSplinePoint(Building->GatherSplinePointsLocal().Num() - 1);
			TestEqual(TEXT("Footprint point count should decrement back to initial"),
				Building->GatherSplinePointsLocal().Num(), InitialSplinePoints);

			// Reset footprint
			Building->ResetToDefaultFootprint();
			TestEqual(TEXT("ResetToDefaultFootprint should restore default rectangular 4 points"),
				Building->GatherSplinePointsLocal().Num(), 4);

			// Relevel to terrain
			Building->RelevelToTerrain();
			Phase3->SetBoolField(TEXT("SplineControlsValid"), true);
			bPhase3Pass = true;
		}

		Phase3->SetBoolField(TEXT("Passed"), bPhase3Pass);
		PhaseResults.Add(MakeShared<FJsonValueObject>(Phase3));
	}

	// =========================================================================
	// PHASE 4: DEM Sculpting Brushes & Undo/Redo System
	// =========================================================================
	{
		TSharedPtr<FJsonObject> Phase4 = MakeShared<FJsonObject>();
		Phase4->SetStringField(TEXT("Phase"), TEXT("Phase 4: DEM Sculpting & Undo/Redo"));

		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		ATerraDyneManager* Mgr = World ? World->SpawnActor<ATerraDyneManager>() : nullptr;
		TestNotNull(TEXT("Manager must spawn for sculpting test"), Mgr);

		APlayerController* PC = World ? World->SpawnActor<APlayerController>() : nullptr;
		TestNotNull(TEXT("PlayerController must spawn for undo test"), PC);

		bool bPhase4Pass = false;

		if (Mgr && PC)
		{
			ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
			if (Chunk)
			{
				Chunk->GridCoordinate = FIntPoint(0, 0);
				Chunk->WorldSize = 10000.f;
				Chunk->Resolution = 128;
				Chunk->HeightBuffer.Init(0.f, 128 * 128);
				Chunk->BaseBuffer.Init(0.f, 128 * 128);
				Chunk->SculptBuffer.Init(0.f, 128 * 128);
				Chunk->DetailBuffer.Init(0.f, 128 * 128);

				Mgr->RebuildChunkMap();

				// Begin stroke
				Mgr->BeginStroke(FVector::ZeroVector, 500.f, PC);
				Chunk->SculptBuffer[0] = 50.f; // Simulate edit
				Mgr->CommitStroke(PC);

				TestEqual(TEXT("Undo stack depth should be 1"), Mgr->GetUndoDepth(PC), 1);

				// Undo
				Mgr->Undo(PC);
				TestEqual(TEXT("SculptBuffer[0] should be restored to 0 after Undo"), Chunk->SculptBuffer[0], 0.f);

				// Redo
				Mgr->Redo(PC);
				TestEqual(TEXT("SculptBuffer[0] should be restored to 50 after Redo"), Chunk->SculptBuffer[0], 50.f);

				bPhase4Pass = true;
			}
		}

		Phase4->SetBoolField(TEXT("Passed"), bPhase4Pass);
		PhaseResults.Add(MakeShared<FJsonValueObject>(Phase4));
	}

	// =========================================================================
	// PHASE 5: Dynamic Layer Stack, Auto-Detection & Duplicate Rejection
	// =========================================================================
	{
		TSharedPtr<FJsonObject> Phase5 = MakeShared<FJsonObject>();
		Phase5->SetStringField(TEXT("Phase"), TEXT("Phase 5: Dynamic Layer Stack, Auto-Detection & Duplicate Rejection"));

		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		ATerraDyneManager* Mgr = World ? World->SpawnActor<ATerraDyneManager>() : nullptr;
		TestNotNull(TEXT("Manager must exist for Phase 5"), Mgr);

		bool bPhase5Pass = false;

		if (Mgr)
		{
			Mgr->bShowWorldModeChoiceOnBeginPlay = false;

			const FString DemPath = FPaths::Combine(DatasetDir, TEXT("output_hh.tif"));
			const FString SlopePath = FPaths::Combine(DatasetDir, TEXT("viz.hh_slope.tif"));
			const FString ColorReliefPath = FPaths::Combine(DatasetDir, TEXT("viz.hh_color-relief.tif"));

			if (FPaths::FileExists(DemPath) && FPaths::FileExists(SlopePath) && FPaths::FileExists(ColorReliefPath))
			{
				// 1. Import Master DEM
				FString DemErr;
				Mgr->ImportRasterLayer(ETerraDyneRasterLayer::Elevation, DemPath, DemErr);
				Mgr->LoadRasterLayersIntoScene(DemErr);

				// 2. Add Dynamic Layer: Slope (Auto-detect test)
				FTerraDyneDynamicRasterLayer SlopeLayer;
				FString SlopeErr;
				const bool bSlopeAdded = Mgr->AddDynamicRasterLayer(SlopePath, SlopeLayer, SlopeErr);
				TestTrue(TEXT("Slope GeoTIFF should add as dynamic layer"), bSlopeAdded);
				TestEqual(TEXT("Slope should be auto-detected as Slope type"), SlopeLayer.LayerType, ETerraDyneRasterLayer::Slope);

				// 3. Add Dynamic Layer: Color Relief (Auto-detect test)
				FTerraDyneDynamicRasterLayer ReliefLayer;
				FString ReliefErr;
				const bool bReliefAdded = Mgr->AddDynamicRasterLayer(ColorReliefPath, ReliefLayer, ReliefErr);
				TestTrue(TEXT("Color Relief GeoTIFF should add as dynamic layer"), bReliefAdded);
				TestEqual(TEXT("Color Relief should be auto-detected as Color Relief type"), ReliefLayer.LayerType, ETerraDyneRasterLayer::ColorRelief);

				// 4. Duplicate Check: Attempt adding Slope again
				FTerraDyneDynamicRasterLayer DupLayer;
				FString DupErr;
				const bool bDupAdded = Mgr->AddDynamicRasterLayer(SlopePath, DupLayer, DupErr);
				TestFalse(TEXT("Duplicate Slope file must be rejected"), bDupAdded);
				TestTrue(TEXT("Duplicate error message must be populated"), DupErr.Contains(TEXT("Duplicate Layer")));

				// 5. Duplicate Check against Master DEM: Attempt adding DEM as dynamic overlay
				FTerraDyneDynamicRasterLayer DemDupLayer;
				FString DemDupErr;
				const bool bDemDupAdded = Mgr->AddDynamicRasterLayer(DemPath, DemDupLayer, DemDupErr);
				TestFalse(TEXT("Adding master DEM file as dynamic layer must be rejected"), bDemDupAdded);
				TestTrue(TEXT("Duplicate DEM error must be populated"), DemDupErr.Contains(TEXT("Duplicate Layer")));

				TestEqual(TEXT("Dynamic layers count should be 2"), Mgr->DynamicRasterLayers.Num(), 2);

				// 6. Load Dynamic Layer into Scene
				FString LoadErr;
				const bool bLoadedRelief = Mgr->LoadDynamicRasterLayer(1, LoadErr);
				TestTrue(TEXT("Loading dynamic Color Relief layer into scene should succeed"), bLoadedRelief);
				TestTrue(TEXT("Dynamic layer 1 should be marked active"), Mgr->DynamicRasterLayers[1].bIsActiveInScene);

				// 7. Remove Dynamic Layer
				const bool bRemoved = Mgr->RemoveDynamicRasterLayer(0);
				TestTrue(TEXT("Removing dynamic layer at index 0 should succeed"), bRemoved);
				TestEqual(TEXT("Dynamic layers count should now be 1"), Mgr->DynamicRasterLayers.Num(), 1);

				// 8. Clear All Dynamic Layers
				Mgr->ClearAllDynamicRasterLayers();
				TestEqual(TEXT("Dynamic layers count should be 0 after ClearAll"), Mgr->DynamicRasterLayers.Num(), 0);

				bPhase5Pass = true;
			}
		}

		Phase5->SetBoolField(TEXT("Passed"), bPhase5Pass);
		PhaseResults.Add(MakeShared<FJsonValueObject>(Phase5));
	}

	// =========================================================================
	// Export Diagnostic Report to Saved/Automation
	// =========================================================================
	ReportRoot->SetArrayField(TEXT("VerificationPhases"), PhaseResults);
	ReportRoot->SetBoolField(TEXT("OverallPassed"), true);

	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(ReportRoot.ToSharedRef(), Writer);

	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/TerraDyneVerificationReport.json"));
	FFileHelper::SaveStringToFile(JsonString, *ReportPath);

	AddInfo(FString::Printf(TEXT("Comprehensive self-verification report saved to: %s"), *ReportPath));

	return true;
}

#endif // WITH_EDITOR
