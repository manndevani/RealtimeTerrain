// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadPierPlacement.h"

// Verifies RoadPierPlacement::PlacePiers against a synthetic "flat, then elevated, then flat"
// road: ground is flat at Z=0 throughout; the road's underside sits only 50cm above it (well
// under the clearance threshold) for the first and last thirds, and 500cm above it (a genuine
// flyover deck) for the middle third -- confirming piers appear only in the elevated stretch, at
// roughly the requested spacing, with the correct height and ground-level base position.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadPierPlacementTest, "TerraDyne.RoadPierPlacement.ElevatedStretchOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadPierPlacementTest::RunTest(const FString& Parameters)
{
	TArray<double> Distances;
	TArray<FVector2D> StationXY;
	TArray<double> RoadUndersideZ;
	TArray<double> GroundZ;

	// 21 stations, 100cm apart (dist 0..2000). Elevated deck spans stations 5-15 (dist 500-1500).
	for (int32 i = 0; i <= 20; ++i)
	{
		const double Dist = i * 100.0;
		Distances.Add(Dist);
		StationXY.Add(FVector2D(Dist, 0.0));
		GroundZ.Add(0.0);
		RoadUndersideZ.Add((i >= 5 && i <= 15) ? 500.0 : 50.0);
	}

	const double MinGap = 150.0;
	const double SpacingAlongRoad = 300.0;

	TArray<RoadPierPlacement::FPierPlacement> Placements;
	RoadPierPlacement::PlacePiers(Distances, StationXY, RoadUndersideZ, GroundZ, MinGap, SpacingAlongRoad, Placements);

	AddInfo(FString::Printf(TEXT("Placements: %d"), Placements.Num()));
	for (const RoadPierPlacement::FPierPlacement& P : Placements)
	{
		AddInfo(FString::Printf(TEXT("  X=%.1f Height=%.1f"), P.LocationXY.X, P.Height));
	}

	// Targets walked are 0,300,600,...,1800 (9 total, since Target<=TotalLength=2000 also admits
	// 2100? no -- loop stops once Target exceeds TotalLength, so last is 1800, next would be 2100 >
	// 2000). Of those, only targets landing on a station within [500,1500] (600,900,1200,1500)
	// clear the gap threshold -- 300 and 1800 bracket the deck but land on the 50cm-gap approach/
	// departure stations once the cursor resolves to the nearest station at or before the target.
	TestTrue(TEXT("At least one pier placed"), Placements.Num() > 0);
	TestTrue(TEXT("No more than the elevated stretch's stations could produce piers"), Placements.Num() <= 5);

	for (const RoadPierPlacement::FPierPlacement& P : Placements)
	{
		TestTrue(TEXT("Every placed pier sits within the elevated stretch (dist 500-1500)"), P.LocationXY.X >= 500.0 && P.LocationXY.X <= 1500.0);
		TestEqual(TEXT("Every placed pier spans the full 500cm deck height"), P.Height, 500.0, 0.01);
		TestEqual(TEXT("Every placed pier bases at ground level"), P.GroundZ, 0.0, 0.01);
	}

	// A perfectly flat road (gap always below threshold) should place no piers at all.
	TArray<double> FlatUnderside;
	for (int32 i = 0; i <= 20; ++i) FlatUnderside.Add(50.0);
	TArray<RoadPierPlacement::FPierPlacement> FlatPlacements;
	RoadPierPlacement::PlacePiers(Distances, StationXY, FlatUnderside, GroundZ, MinGap, SpacingAlongRoad, FlatPlacements);
	TestEqual(TEXT("A flat road (gap under threshold everywhere) gets no piers"), FlatPlacements.Num(), 0);

	return true;
}
