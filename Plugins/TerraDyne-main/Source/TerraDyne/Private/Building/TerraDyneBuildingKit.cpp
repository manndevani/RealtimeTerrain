// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/TerraDyneBuildingKit.h"
#include "Engine/StaticMesh.h"

namespace
{
	const TCHAR* const KitRoot = TEXT("/Game/SD_Art/Industrial_Architecture/Assets");
}

UTerraDyneBuildingKit::UTerraDyneBuildingKit()
{
	// Defaults point at the SD_Art Industrial Architecture kit so a building with no Kit assigned
	// still generates. Widths below are the measured Y extents of the meshes, not the filenames —
	// the "4x2M" naming is height-first, so SM_Wall_Panel_4x2M is 4 m tall and 2 m wide.
	WallPanels = {
		{ 800, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/Wall_Panels/SM_Wall_Panel_Beams_4x8M.SM_Wall_Panel_Beams_4x8M"), KitRoot))) },
		{ 400, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/Wall_Panels/SM_Wall_Panel_Beams_4x4M.SM_Wall_Panel_Beams_4x4M"), KitRoot))) },
		{ 200, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/Wall_Panels/SM_Wall_Panel_Beams_4x2M.SM_Wall_Panel_Beams_4x2M"), KitRoot))) }
	};

	WallPanelDoor = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		FString::Printf(TEXT("%s/Wall_Panels/SM_Wall_Panel_Beams_Door_Center_4x4M.SM_Wall_Panel_Beams_Door_Center_4x4M"), KitRoot)));

	CornerPost = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		FString::Printf(TEXT("%s/Wall_Panels/SM_Beam_Corner_Left_4M.SM_Beam_Corner_Left_4M"), KitRoot)));

	// Three slab sizes in a halving chain. The filler drops to a smaller tile wherever a full one
	// would poke out through the walls, which is what happens on any footprint edge that is not a
	// whole number of modules.
	FloorTiles = {
		{ 400, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/Floor/SM_Floor_4x4M.SM_Floor_4x4M"), KitRoot))) },
		{ 200, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/Floor/SM_Floor_2x2M.SM_Floor_2x2M"), KitRoot))) },
		{ 100, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%s/Floor/SM_Floor_1x1M.SM_Floor_1x1M"), KitRoot))) }
	};
}

TArray<int32> UTerraDyneBuildingKit::GetPanelWidthsDescending() const
{
	TArray<int32> Widths;
	Widths.Reserve(WallPanels.Num());

	for (const FTerraDyneWallPanelEntry& Entry : WallPanels)
	{
		if (Entry.WidthUnits > 0)
		{
			Widths.AddUnique(Entry.WidthUnits);
		}
	}

	if (Widths.Num() == 0)
	{
		Widths = { 800, 400, 200 };
	}

	Widths.Sort([](int32 A, int32 B) { return A > B; });
	return Widths;
}

UStaticMesh* UTerraDyneBuildingKit::ResolvePanelOfWidth(int32 WidthUnits) const
{
	for (const FTerraDyneWallPanelEntry& Entry : WallPanels)
	{
		if (Entry.WidthUnits == WidthUnits && !Entry.Mesh.IsNull())
		{
			if (UStaticMesh* Loaded = Entry.Mesh.LoadSynchronous())
			{
				return Loaded;
			}
		}
	}

	// Graceful fallback to engine cube
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

int32 UTerraDyneBuildingKit::GetSmallestPanelWidth() const
{
	int32 Smallest = 0;
	for (const FTerraDyneWallPanelEntry& Entry : WallPanels)
	{
		if (Entry.WidthUnits > 0)
		{
			Smallest = (Smallest == 0) ? Entry.WidthUnits : FMath::Min(Smallest, Entry.WidthUnits);
		}
	}
	return Smallest > 0 ? Smallest : 200;
}

TArray<FTerraDyneFloorTileEntry> UTerraDyneBuildingKit::GetFloorTilesDescending() const
{
	TArray<FTerraDyneFloorTileEntry> Tiles;
	for (const FTerraDyneFloorTileEntry& Entry : FloorTiles)
	{
		if (Entry.SizeUnits > 0)
		{
			Tiles.Add(Entry);
		}
	}

	if (Tiles.Num() == 0)
	{
		Tiles = {
			{ 400, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))) },
			{ 200, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))) },
			{ 100, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))) }
		};
	}

	Tiles.Sort([](const FTerraDyneFloorTileEntry& A, const FTerraDyneFloorTileEntry& B)
	{
		return A.SizeUnits > B.SizeUnits;
	});
	return Tiles;
}

TArray<FTerraDyneFloorTileEntry> UTerraDyneBuildingKit::GetRoofTilesDescending() const
{
	TArray<FTerraDyneFloorTileEntry> Tiles;
	for (const FTerraDyneFloorTileEntry& Entry : RoofTiles)
	{
		if (Entry.SizeUnits > 0)
		{
			Tiles.Add(Entry);
		}
	}

	if (Tiles.Num() == 0)
	{
		return GetFloorTilesDescending();
	}

	Tiles.Sort([](const FTerraDyneFloorTileEntry& A, const FTerraDyneFloorTileEntry& B)
	{
		return A.SizeUnits > B.SizeUnits;
	});
	return Tiles;
}

bool UTerraDyneBuildingKit::IsUsable() const
{
	return ModuleWidth > 0.0f && FloorHeight > 0.0f;
}
