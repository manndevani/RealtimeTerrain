// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Core/GRTManager.h"
#include "UI/GRTToolWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "Cesium3DTileset.h"
#include "CesiumUrlTemplateRasterOverlay.h"
#include "CesiumPolygonRasterOverlay.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// Exercises AGRTManager::ImportCesiumTerrainTileset/ImportCesiumImageryOverlay against
// synthetic on-disk pyramids -- these only need a layer.json file and a folder tree with numeric
// zoom subfolders, not real quantized-mesh/PNG payloads, since both entry points validate
// structure and configure Cesium actors before ever fetching a tile. Nothing here depends on the
// machine-local real-world datasets GRTGeoTIFFRealFileTests/GRTRasterLayerTests use,
// so this runs identically everywhere.
namespace GRTTiledCesiumImportTest_Private
{
	FString ScratchRoot()
	{
		return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"), TEXT("TiledCesiumImportTest"));
	}

	/** A folder with no layer.json in it -- the "not a tileset" case. */
	FString MakeEmptyFolder(const TCHAR* SubDir)
	{
		const FString Path = FPaths::Combine(ScratchRoot(), SubDir);
		IFileManager::Get().MakeDirectory(*Path, true);
		return Path;
	}

	/**
	 * A folder containing layer.json -- ImportCesiumTerrainTileset only checks for its presence, but
	 * the real ACesium3DTileset it points at DOES parse the content asynchronously, and logs a real
	 * Cesium-severity error for a structurally incomplete one ("does not specify any tile URL
	 * templates"). That error surfaces async and can land inside a LATER test's log window rather
	 * than this one's, failing an unrelated test purely from log noise -- so this is a real (if
	 * minimal) layer.json, matching the shape a real Cesium export has, not just enough to satisfy
	 * FPaths::FileExists.
	 */
	FString MakeTerrainFolder(const TCHAR* SubDir)
	{
		const FString Path = FPaths::Combine(ScratchRoot(), SubDir);
		IFileManager::Get().MakeDirectory(*Path, true);
		FFileHelper::SaveStringToFile(
			TEXT("{\"tilejson\":\"2.1.0\",\"format\":\"quantized-mesh-1.0\",\"version\":\"1.0.0\",")
			TEXT("\"scheme\":\"tms\",\"tiles\":[\"{z}/{x}/{y}.terrain\"],\"projection\":\"EPSG:4326\",")
			TEXT("\"bounds\":[-180,-90,180,90],\"minzoom\":0,\"maxzoom\":0}"),
			*FPaths::Combine(Path, TEXT("layer.json")));
		return Path;
	}

	/**
	 * As MakeTerrainFolder, but also touches a single empty {Zoom}/{X}/{Y}.terrain tile file, so
	 * ComputeQuantizedMeshGeographicBounds has a real tile index to scan.
	 */
	FString MakeTerrainFolderWithOneTile(const TCHAR* SubDir, int32 Zoom, int32 X, int32 Y)
	{
		const FString Path = MakeTerrainFolder(SubDir);
		const FString XDir = FPaths::Combine(Path, FString::FromInt(Zoom), FString::FromInt(X));
		IFileManager::Get().MakeDirectory(*XDir, true);
		FFileHelper::SaveStringToFile(TEXT(""), *FPaths::Combine(XDir, FString::Printf(TEXT("%d.terrain"), Y)));
		return Path;
	}

	/**
	 * A folder with numeric zoom-level subfolders MinZoom..MaxZoom (plus a non-numeric folder and a
	 * stray file, to prove DetectZoomRange ignores both) -- no actual PNGs needed, since zoom
	 * detection only reads directory names.
	 */
	FString MakeImageryPyramidFolder(const TCHAR* SubDir, int32 MinZoom, int32 MaxZoom)
	{
		const FString Path = FPaths::Combine(ScratchRoot(), SubDir);
		IFileManager::Get().MakeDirectory(*Path, true);
		for (int32 Zoom = MinZoom; Zoom <= MaxZoom; ++Zoom)
		{
			IFileManager::Get().MakeDirectory(*FPaths::Combine(Path, FString::FromInt(Zoom)), true);
		}
		IFileManager::Get().MakeDirectory(*FPaths::Combine(Path, TEXT("not_a_zoom_level")), true);
		FFileHelper::SaveStringToFile(TEXT("stray"), *FPaths::Combine(Path, TEXT("readme.txt")));
		return Path;
	}

	/**
	 * As MakeImageryPyramidFolder, but also touches a single empty {MaxZoom}/{X}/{Y}.png tile file,
	 * so ComputeWebMercatorGeographicBounds has a real tile index to scan.
	 */
	FString MakeImageryPyramidFolderWithOneTile(const TCHAR* SubDir, int32 MinZoom, int32 MaxZoom, int32 X, int32 Y)
	{
		const FString Path = MakeImageryPyramidFolder(SubDir, MinZoom, MaxZoom);
		const FString XDir = FPaths::Combine(Path, FString::FromInt(MaxZoom), FString::FromInt(X));
		IFileManager::Get().MakeDirectory(*XDir, true);
		FFileHelper::SaveStringToFile(TEXT(""), *FPaths::Combine(XDir, FString::Printf(TEXT("%d.png"), Y)));
		return Path;
	}

	/**
	 * Walks the real Slate widget tree collecting the text of every STextBlock that is actually
	 * visible -- meaning every ancestor up to the root is not Collapsed/Hidden, not just present
	 * somewhere in the tree. A widget hidden by a collapsed accordion is still IN the tree (Collapsed
	 * only removes layout space), so "the label exists in the tree" alone would not have caught the
	 * bug this test exists to catch: the Tiled Cesium Import accordion defaulting to collapsed while
	 * every sibling section defaults open, which left the section's own header visible but its two
	 * import buttons invisible until a player happened to click it.
	 */
	void CollectVisibleTextLabels(const TSharedRef<SWidget>& Widget, bool bAncestorsVisible, TArray<FString>& OutLabels)
	{
		const bool bThisVisible = bAncestorsVisible && Widget->GetVisibility().IsVisible();

		if (bThisVisible && Widget->GetType() == FName(TEXT("STextBlock")))
		{
			OutLabels.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
		}

		if (FChildren* Children = Widget->GetChildren())
		{
			const int32 NumChildren = Children->Num();
			for (int32 Index = 0; Index < NumChildren; ++Index)
			{
				CollectVisibleTextLabels(Children->GetChildAt(Index), bThisVisible, OutLabels);
			}
		}
	}

	bool AnyLabelContains(const TArray<FString>& Labels, const TCHAR* Substring)
	{
		for (const FString& Label : Labels)
		{
			if (Label.Contains(Substring))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTTiledCesiumImportValidationTest,
	"GRT.TiledImport.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTTiledCesiumImportValidationTest::RunTest(const FString& Parameters)
{
	using namespace GRTTiledCesiumImportTest_Private;

	IFileManager::Get().DeleteDirectory(*ScratchRoot(), false, true);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
	TestNotNull(TEXT("Manager instance should be valid"), Mgr);
	if (!Mgr) return false;
	Mgr->bShowWorldModeChoiceOnBeginPlay = false;

	// DEM-only gate: a fresh manager defaults to Sandbox (WorldOriginKind::Procedural, no elevation
	// layer imported), and BOTH entry points must refuse to run there -- even against otherwise
	// well-formed folders, proving the rejection is the mode gate and not a folder-content check.
	{
		TestEqual(TEXT("A freshly spawned manager should default to Sandbox"),
			(uint8)Mgr->WorldOriginKind, (uint8)EGRTChunkOrigin::Procedural);
		TestFalse(TEXT("A freshly spawned manager should have no elevation layer"), Mgr->HasElevationLayer());

		FVector2D UnusedCenter;
		double UnusedSpan = 0.0;

		const FString TerrainFolder = MakeTerrainFolder(TEXT("SandboxTerrain"));
		FString TerrainError;
		TestFalse(TEXT("Terrain tileset import must be refused in a Sandbox world"),
			Mgr->ImportCesiumTerrainTileset(TerrainFolder, TerrainError, UnusedCenter, UnusedSpan));
		TestTrue(TEXT("Sandbox rejection error should say so"), TerrainError.Contains(TEXT("Sandbox")));
		TestNull(TEXT("No tileset actor should be spawned when blocked by the Sandbox gate"), Mgr->GetActiveTiledCesiumTileset());

		const FString ImageryFolder = MakeImageryPyramidFolder(TEXT("SandboxImagery"), 0, 5);
		FString ImageryError;
		TestFalse(TEXT("Imagery overlay import must be refused in a Sandbox world"),
			Mgr->ImportCesiumImageryOverlay(ImageryFolder, ImageryError, UnusedCenter, UnusedSpan));
		TestTrue(TEXT("Sandbox rejection error should say so"), ImageryError.Contains(TEXT("Sandbox")));
		TestNull(TEXT("No overlay component should be spawned when blocked by the Sandbox gate"), Mgr->GetActiveTiledCesiumOverlay());
	}

	// Switch to a DEM world (the same signal SGRTPanel::GetGeoTIFFSectionVisibility checks)
	// before exercising the folder-content validation below, so those checks land on the actual
	// layer.json/zoom-subfolder logic instead of being short-circuited by the mode gate above.
	Mgr->WorldOriginKind = EGRTChunkOrigin::ImportedGeoTIFF;

	// A folder with no layer.json must be refused, with an error that actually names the problem --
	// not just "false", since a wrong-but-plausible error message is as unhelpful as none.
	{
		const FString Folder = MakeEmptyFolder(TEXT("NotATileset"));
		FString OutError;
		FVector2D UnusedCenter;
		double UnusedSpan = 0.0;
		const bool bImported = Mgr->ImportCesiumTerrainTileset(Folder, OutError, UnusedCenter, UnusedSpan);
		TestFalse(TEXT("A folder with no layer.json must be rejected as a terrain tileset"), bImported);
		TestTrue(TEXT("Error should mention layer.json"), OutError.Contains(TEXT("layer.json")));
		TestNull(TEXT("No tileset actor should be spawned on a rejected import"), Mgr->GetActiveTiledCesiumTileset());
	}

	// A folder with no numeric zoom subfolders must be refused the same way for the overlay side.
	{
		const FString Folder = MakeEmptyFolder(TEXT("NotAPyramid"));
		FString OutError;
		FVector2D UnusedCenter;
		double UnusedSpan = 0.0;
		const bool bImported = Mgr->ImportCesiumImageryOverlay(Folder, OutError, UnusedCenter, UnusedSpan);
		TestFalse(TEXT("A folder with no numeric zoom subfolders must be rejected as an imagery pyramid"), bImported);
		TestTrue(TEXT("Error should mention the expected z/x/y.png layout"), OutError.Contains(TEXT("z}/{x}/{y}.png")));
		TestNull(TEXT("No overlay component should exist on a rejected import"), Mgr->GetActiveTiledCesiumOverlay());
	}

	IFileManager::Get().DeleteDirectory(*ScratchRoot(), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTTiledCesiumImportWorkflowTest,
	"GRT.TiledImport.Workflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTTiledCesiumImportWorkflowTest::RunTest(const FString& Parameters)
{
	using namespace GRTTiledCesiumImportTest_Private;

	IFileManager::Get().DeleteDirectory(*ScratchRoot(), false, true);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
	TestNotNull(TEXT("Manager instance should be valid"), Mgr);
	if (!Mgr) return false;
	Mgr->bShowWorldModeChoiceOnBeginPlay = false;

	// This workflow exercises the tiled import itself, not the DEM-only gate (that is covered by
	// FGRTTiledCesiumImportValidationTest), so switch to a DEM world upfront.
	Mgr->WorldOriginKind = EGRTChunkOrigin::ImportedGeoTIFF;

	// Stands in for a placed "Cesium World Terrain" backdrop tileset -- ImportCesiumTerrainTileset
	// must hide actors like this once a local terrain import covers the same ground, or the two
	// meshes visibly overlap/z-fight (this is what the user actually saw and reported).
	ACesium3DTileset* BackdropTileset = World->SpawnActor<ACesium3DTileset>();
	TestNotNull(TEXT("Backdrop tileset should spawn"), BackdropTileset);
	if (BackdropTileset)
	{
		TestFalse(TEXT("Backdrop tileset should start visible"), BackdropTileset->IsHidden());
	}

	// Importing imagery with no terrain tileset yet must spawn a bare ellipsoid tileset for the
	// overlay to attach to, not fail or leave the overlay unattached.
	ACesium3DTileset* SpawnedTileset = nullptr;
	{
		const FString Folder = MakeImageryPyramidFolder(TEXT("Imagery"), 0, 7);
		FString OutError;
		FVector2D UnusedCenter;
		double UnusedSpan = 0.0;
		const bool bImported = Mgr->ImportCesiumImageryOverlay(Folder, OutError, UnusedCenter, UnusedSpan);
		TestTrue(FString::Printf(TEXT("Imagery-only import should succeed (err: %s)"), *OutError), bImported);

		SpawnedTileset = Mgr->GetActiveTiledCesiumTileset();
		TestNotNull(TEXT("A bare tileset should be auto-spawned for the overlay"), SpawnedTileset);
		if (SpawnedTileset)
		{
			TestEqual(TEXT("Auto-spawned tileset should be FromEllipsoid until real terrain is imported"),
				SpawnedTileset->GetTilesetSource(), ETilesetSource::FromEllipsoid);
		}

		UCesiumUrlTemplateRasterOverlay* Overlay = Mgr->GetActiveTiledCesiumOverlay();
		TestNotNull(TEXT("Overlay component should be spawned"), Overlay);
		if (Overlay)
		{
			// Assert the actual configured values, not just that something non-null exists -- the
			// whole point of DetectZoomRange is that it reads these from disk rather than hardcoding
			// them, so the test has to prove it read the RIGHT values.
			TestEqual(TEXT("MinimumLevel should be read from the lowest zoom subfolder"), Overlay->MinimumLevel, 0);
			TestEqual(TEXT("MaximumLevel should be read from the highest zoom subfolder"), Overlay->MaximumLevel, 7);
			TestEqual(TEXT("Non-numeric subfolders must not affect the detected range"), Overlay->MaximumLevel, 7);
			TestEqual(TEXT("Projection should default to WebMercator for a local XYZ pyramid"),
				Overlay->Projection, ECesiumUrlTemplateRasterOverlayProjection::WebMercator);
			TestEqual(TEXT("TileWidth should be the standard 256"), Overlay->TileWidth, 256);
			TestEqual(TEXT("TileHeight should be the standard 256"), Overlay->TileHeight, 256);
			TestTrue(TEXT("TemplateUrl should be a local file:/// URL"), Overlay->TemplateUrl.StartsWith(TEXT("file:///")));
			// {reverseY}, not {y} -- see the comment on this string's construction in
			// GRTManagerTiledCesiumImport.cpp. {y} means TMS row-ordering; these locally-exported
			// pyramids are XYZ, so {y} would silently request the wrong row from a real tile server too.
			TestTrue(TEXT("TemplateUrl should end with the z/x/reverseY.png pattern"), Overlay->TemplateUrl.EndsWith(TEXT("/{z}/{x}/{reverseY}.png")));
			TestTrue(TEXT("TemplateUrl should point at the picked folder"), Overlay->TemplateUrl.Contains(TEXT("Imagery")));
		}

		if (BackdropTileset)
		{
			TestFalse(TEXT("Imagery-only import must NOT hide the backdrop -- the bare ellipsoid has no relief of its own to compete with it"),
				BackdropTileset->IsHidden());
			TestNull(TEXT("Imagery-only import must NOT clip the backdrop either"),
				BackdropTileset->FindComponentByClass<UCesiumPolygonRasterOverlay>());
		}
	}

	// Importing a real terrain tileset afterwards must repoint the SAME actor rather than spawning
	// a second one -- two independently-managed tileset actors would double-render and double-cost.
	{
		// A real tile is required here (not the bare-layer.json MakeTerrainFolder) so the import
		// actually determines a footprint -- the backdrop cutout below only engages once one exists.
		const FString Folder = MakeTerrainFolderWithOneTile(TEXT("Terrain"), 0, 1, 0);
		FString OutError;
		FVector2D UnusedCenter;
		double UnusedSpan = 0.0;
		const bool bImported = Mgr->ImportCesiumTerrainTileset(Folder, OutError, UnusedCenter, UnusedSpan);
		TestTrue(FString::Printf(TEXT("Terrain import should succeed (err: %s)"), *OutError), bImported);

		ACesium3DTileset* TilesetAfter = Mgr->GetActiveTiledCesiumTileset();
		TestEqual(TEXT("Terrain import must repoint the existing tileset actor, not spawn a new one"),
			TilesetAfter, SpawnedTileset);
		if (TilesetAfter)
		{
			TestEqual(TEXT("TilesetSource should switch to FromUrl once real terrain is imported"),
				TilesetAfter->GetTilesetSource(), ETilesetSource::FromUrl);
			const FString UrlAfter = TilesetAfter->GetUrl();
			TestTrue(TEXT("Url should be a local file:/// URL"), UrlAfter.StartsWith(TEXT("file:///")));
			TestTrue(TEXT("Url should point at layer.json in the picked folder"), UrlAfter.EndsWith(TEXT("/layer.json")));
			TestTrue(TEXT("Url should point at the picked folder"), UrlAfter.Contains(TEXT("Terrain")));
		}

		if (BackdropTileset)
		{
			TestFalse(TEXT("Terrain import must NOT hide the backdrop -- it should stay visible everywhere except the cutout"),
				BackdropTileset->IsHidden());
			UCesiumPolygonRasterOverlay* ClipOverlay = BackdropTileset->FindComponentByClass<UCesiumPolygonRasterOverlay>();
			TestNotNull(TEXT("Terrain import must clip the backdrop so it doesn't visibly overlap the newly-imported local terrain"), ClipOverlay);
			if (ClipOverlay)
			{
				TestTrue(TEXT("ExcludeSelectedTiles should stay on its default of true (that's what actually cuts the hole)"), ClipOverlay->ExcludeSelectedTiles);
				TestFalse(TEXT("InvertSelection should stay off -- cut the imported footprint OUT, not everything else"), ClipOverlay->InvertSelection);
			}
		}
		if (TilesetAfter)
		{
			TestFalse(TEXT("The local tileset itself must stay visible"), TilesetAfter->IsHidden());
		}

		// Turning the cutout off must remove the clip and leave the backdrop fully intact again.
		if (BackdropTileset)
		{
			Mgr->SetCutOutBackdropForTiledImport(false);
			TestNull(TEXT("Disabling the cutout must remove the clip overlay"),
				BackdropTileset->FindComponentByClass<UCesiumPolygonRasterOverlay>());
			TestFalse(TEXT("Disabling the cutout must not hide the backdrop either"), BackdropTileset->IsHidden());

			// Re-enabling it must re-apply the clip from the last-known footprint, without needing to
			// re-import anything.
			Mgr->SetCutOutBackdropForTiledImport(true);
			TestNotNull(TEXT("Re-enabling the cutout must re-apply the clip"),
				BackdropTileset->FindComponentByClass<UCesiumPolygonRasterOverlay>());
		}

		// The height-offset slider must move the tileset actor, absolutely (not cumulatively).
		if (TilesetAfter)
		{
			Mgr->SetTiledCesiumHeightOffset(250.0);
			TestTrue(TEXT("A 250m height offset should move the tileset ~25000cm up"),
				FMath::IsNearlyEqual(TilesetAfter->GetActorLocation().Z, 25000.0, 1.0));

			Mgr->SetTiledCesiumHeightOffset(250.0);
			TestTrue(TEXT("Setting the SAME offset again must not double it (absolute, not cumulative)"),
				FMath::IsNearlyEqual(TilesetAfter->GetActorLocation().Z, 25000.0, 1.0));

			Mgr->SetTiledCesiumHeightOffset(0.0);
			TestTrue(TEXT("Resetting the offset to 0 should return the tileset to its base position"),
				FMath::IsNearlyEqual(TilesetAfter->GetActorLocation().Z, 0.0, 1.0));
		}
	}

	// Re-importing imagery with a different zoom range must repoint the SAME overlay component and
	// update its levels, not leave stale values from the first import or spawn a duplicate.
	{
		UCesiumUrlTemplateRasterOverlay* OverlayBefore = Mgr->GetActiveTiledCesiumOverlay();

		const FString Folder = MakeImageryPyramidFolder(TEXT("ImageryV2"), 2, 12);
		FString OutError;
		FVector2D UnusedCenter;
		double UnusedSpan = 0.0;
		const bool bImported = Mgr->ImportCesiumImageryOverlay(Folder, OutError, UnusedCenter, UnusedSpan);
		TestTrue(FString::Printf(TEXT("Second imagery import should succeed (err: %s)"), *OutError), bImported);

		UCesiumUrlTemplateRasterOverlay* OverlayAfter = Mgr->GetActiveTiledCesiumOverlay();
		TestEqual(TEXT("Re-importing imagery must repoint the existing overlay component, not spawn a new one"),
			OverlayAfter, OverlayBefore);
		if (OverlayAfter)
		{
			TestEqual(TEXT("MinimumLevel should update to the new pyramid's range"), OverlayAfter->MinimumLevel, 2);
			TestEqual(TEXT("MaximumLevel should update to the new pyramid's range"), OverlayAfter->MaximumLevel, 12);
			TestTrue(TEXT("TemplateUrl should update to the new folder"), OverlayAfter->TemplateUrl.Contains(TEXT("ImageryV2")));
		}
	}

	IFileManager::Get().DeleteDirectory(*ScratchRoot(), false, true);
	return true;
}

// Exercises the real SGRTPanel Slate widget tree (built the same way GRTToolWidget
// itself builds it, via UUserWidget::TakeWidget -> RebuildWidget), not just the manager functions
// behind it. GRT.TiledImport.Workflow proves the import calls do the right thing; this proves
// a player can actually SEE and reach them -- the two are independent failure modes, and the second
// one is exactly what regressed once already (see CollectVisibleTextLabels's comment).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTTiledCesiumImportPanelUITest,
	"GRT.TiledImport.PanelUI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTTiledCesiumImportPanelUITest::RunTest(const FString& Parameters)
{
	using namespace GRTTiledCesiumImportTest_Private;

	// Sandbox: the DEM-only gate must hide the ENTIRE section, header included -- not just leave the
	// buttons present-but-disabled, since ImportCesiumTerrainTileset/ImportCesiumImageryOverlay both
	// refuse to run here (see FGRTTiledCesiumImportValidationTest) and a visible dead-end control
	// is worse UX than no control at all.
	{
		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		TestNotNull(TEXT("World must be valid"), World);
		if (!World) return false;

		AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
		TestNotNull(TEXT("Manager instance should be valid"), Mgr);
		if (!Mgr) return false;
		Mgr->bShowWorldModeChoiceOnBeginPlay = false;
		// WorldOriginKind is left at its default (Procedural / Sandbox) deliberately.

		UGRTToolWidget* ToolWidget = CreateWidget<UGRTToolWidget>(World);
		TestNotNull(TEXT("ToolWidget should construct"), ToolWidget);
		if (!ToolWidget) return false;

		// A widget tree that has never been added to a live window or prepassed reads back each
		// bound Visibility attribute's default CACHED value (Visible), not the live delegate result --
		// TSlateAttribute only re-invokes its bound getter during Prepass_ChildLoop's
		// UpdateOnlyVisibilityAttributes call. Without this, every gated section would read as visible
		// regardless of the manager's actual mode, making the test worthless.
		TSharedRef<SWidget> Root = ToolWidget->TakeWidget();
		Root->SlatePrepass();

		TArray<FString> Labels;
		CollectVisibleTextLabels(Root, true, Labels);

		TestFalse(TEXT("Sandbox: the Tiled Cesium Import section header must not be visible"),
			AnyLabelContains(Labels, TEXT("TILED CESIUM IMPORT")));
		TestFalse(TEXT("Sandbox: the terrain-tileset import button must not be visible"),
			AnyLabelContains(Labels, TEXT("Import Terrain Tileset Folder")));
		TestFalse(TEXT("Sandbox: the imagery-overlay import button must not be visible"),
			AnyLabelContains(Labels, TEXT("Import Imagery Overlay Folder")));
	}

	// DEM world: the section header, both buttons, and the status text all have to actually render.
	{
		UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
		TestNotNull(TEXT("World must be valid"), World);
		if (!World) return false;

		AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
		TestNotNull(TEXT("Manager instance should be valid"), Mgr);
		if (!Mgr) return false;
		Mgr->bShowWorldModeChoiceOnBeginPlay = false;
		Mgr->WorldOriginKind = EGRTChunkOrigin::ImportedGeoTIFF;

		UGRTToolWidget* ToolWidget = CreateWidget<UGRTToolWidget>(World);
		TestNotNull(TEXT("ToolWidget should construct"), ToolWidget);
		if (!ToolWidget) return false;

		TSharedRef<SWidget> Root = ToolWidget->TakeWidget();
		Root->SlatePrepass();

		TArray<FString> Labels;
		CollectVisibleTextLabels(Root, true, Labels);

		TestTrue(TEXT("DEM: the Tiled Cesium Import section header should be visible"),
			AnyLabelContains(Labels, TEXT("TILED CESIUM IMPORT")));
		TestTrue(TEXT("DEM: the terrain-tileset import button should be visible (accordion must default open)"),
			AnyLabelContains(Labels, TEXT("Import Terrain Tileset Folder")));
		TestTrue(TEXT("DEM: the imagery-overlay import button should be visible (accordion must default open)"),
			AnyLabelContains(Labels, TEXT("Import Imagery Overlay Folder")));
		TestTrue(TEXT("DEM: the status text should be visible"),
			AnyLabelContains(Labels, TEXT("No tileset or overlay loaded yet")));
	}

	return true;
}

// Verifies the actual tile-index -> lon/lat math behind "fly the camera to where the tiles are"
// against hand-computed expected values, not just "a non-zero number came back". Both formulas
// (TMS geographic for quantized-mesh terrain, WebMercator/slippy-map for XYZ imagery) are worked
// out independently below rather than by re-deriving them the same way the production code does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTTiledCesiumImportGeographicFootprintTest,
	"GRT.TiledImport.GeographicFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTTiledCesiumImportGeographicFootprintTest::RunTest(const FString& Parameters)
{
	using namespace GRTTiledCesiumImportTest_Private;

	IFileManager::Get().DeleteDirectory(*ScratchRoot(), false, true);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	AGRTManager* Mgr = World->SpawnActor<AGRTManager>();
	TestNotNull(TEXT("Manager instance should be valid"), Mgr);
	if (!Mgr) return false;
	Mgr->bShowWorldModeChoiceOnBeginPlay = false;
	Mgr->WorldOriginKind = EGRTChunkOrigin::ImportedGeoTIFF;

	const double Tolerance = 0.01;

	// Quantized-mesh (TMS geographic): zoom 0's tile x=1 is the globe's EASTERN half -- the tile pair
	// at zoom 0 is [x=0: -180..0] [x=1: 0..180], each spanning the full -90..90 latitude range. Tile
	// (0, 1, 0) should therefore report centre (90, 0), span 180 degrees, independent of anything the
	// production code itself computes internally.
	{
		const FString Folder = MakeTerrainFolderWithOneTile(TEXT("FootprintTerrain"), 0, 1, 0);
		FString OutError;
		FVector2D CenterLonLat;
		double SpanDegrees = 0.0;
		const bool bImported = Mgr->ImportCesiumTerrainTileset(Folder, OutError, CenterLonLat, SpanDegrees);
		TestTrue(FString::Printf(TEXT("Terrain import should succeed (err: %s)"), *OutError), bImported);

		TestTrue(TEXT("Terrain footprint centre longitude should be 90"), FMath::IsNearlyEqual(CenterLonLat.X, 90.0, Tolerance));
		TestTrue(TEXT("Terrain footprint centre latitude should be 0"), FMath::IsNearlyEqual(CenterLonLat.Y, 0.0, Tolerance));
		TestTrue(TEXT("Terrain footprint span should be 180 degrees"), FMath::IsNearlyEqual(SpanDegrees, 180.0, Tolerance));
	}

	// WebMercator (XYZ): zoom 1's tile (x=1, y=0) is the globe's NE quadrant -- longitude 0..180 (via
	// x=1 of 2), latitude 0..~85.0511 (WebMercator's own northern clamp, via the standard inverse
	// projection: lat(y) = atan(sinh(pi - 2*pi*y/n))). Centre should land at (90, ~42.5256), span 180.
	{
		const FString Folder = MakeImageryPyramidFolderWithOneTile(TEXT("FootprintImagery"), 0, 1, 1, 0);
		FString OutError;
		FVector2D CenterLonLat;
		double SpanDegrees = 0.0;
		const bool bImported = Mgr->ImportCesiumImageryOverlay(Folder, OutError, CenterLonLat, SpanDegrees);
		TestTrue(FString::Printf(TEXT("Imagery import should succeed (err: %s)"), *OutError), bImported);

		TestTrue(TEXT("Imagery footprint centre longitude should be 90"), FMath::IsNearlyEqual(CenterLonLat.X, 90.0, Tolerance));
		TestTrue(TEXT("Imagery footprint centre latitude should be ~42.5256 (WebMercator's own clamp / 2)"),
			FMath::IsNearlyEqual(CenterLonLat.Y, 42.5256, 0.01));
		TestTrue(TEXT("Imagery footprint span should be 180 degrees (the wider of the two axes)"),
			FMath::IsNearlyEqual(SpanDegrees, 180.0, Tolerance));
	}

	IFileManager::Get().DeleteDirectory(*ScratchRoot(), false, true);
	return true;
}

#endif // WITH_EDITOR
