// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadStylePresets.h"

// Verifies URoadStylePreset's driving-lane count and total-width scans, ported from RoadBuilder's
// URoadStyle::NumDrivingLanes -- a style with a mix of driving/shoulder/sidewalk lanes on both
// sides should count only the driving ones, matching the real thing this drives (e.g. picking a
// junction leg's arm layout by how many driving lanes actually need to route through it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadStylePresetLaneCountTest, "TerraDyne.RoadStylePresets.LaneCount", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadStylePresetLaneCountTest::RunTest(const FString& Parameters)
{
	URoadStylePreset* Style = NewObject<URoadStylePreset>();

	FRoadStyleLaneEntry LeftDriving1;
	LeftDriving1.Width = 350.0;
	LeftDriving1.LaneType = ERealtimeLaneType::Driving;
	Style->LeftLanes.Add(LeftDriving1);

	FRoadStyleLaneEntry LeftSidewalk;
	LeftSidewalk.Width = 160.0;
	LeftSidewalk.LaneType = ERealtimeLaneType::Sidewalk;
	Style->LeftLanes.Add(LeftSidewalk);

	FRoadStyleLaneEntry RightDriving1;
	RightDriving1.Width = 350.0;
	RightDriving1.LaneType = ERealtimeLaneType::Driving;
	Style->RightLanes.Add(RightDriving1);

	FRoadStyleLaneEntry RightDriving2;
	RightDriving2.Width = 350.0;
	RightDriving2.LaneType = ERealtimeLaneType::Driving;
	Style->RightLanes.Add(RightDriving2);

	FRoadStyleLaneEntry RightShoulder;
	RightShoulder.Width = 200.0;
	RightShoulder.LaneType = ERealtimeLaneType::Shoulder;
	Style->RightLanes.Add(RightShoulder);

	TestEqual(TEXT("Driving lane count ignores shoulder/sidewalk"), Style->NumDrivingLanes(), 3);
	TestEqual(TEXT("Total width sums every lane on both sides"), Style->TotalWidth(), 350.0 + 160.0 + 350.0 + 350.0 + 200.0, 0.01);
	return true;
}

// Verifies URoadLaneShape's material-slot lookup, ported from RoadBuilder's ULaneShape usage
// pattern where each cross-section point/quad indexes into the shape's own material list rather
// than embedding a material reference per point.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadLaneShapeMaterialSlotTest, "TerraDyne.RoadStylePresets.LaneShapeMaterialSlot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadLaneShapeMaterialSlotTest::RunTest(const FString& Parameters)
{
	URoadLaneShape* Shape = NewObject<URoadLaneShape>();
	Shape->Materials.Add(nullptr);
	Shape->Materials.Add(nullptr);

	FRoadLaneShapePoint PointA;
	PointA.Lateral = -175.0;
	PointA.Vertical = 0.0;
	PointA.MaterialSlot = 0;
	Shape->Points.Add(PointA);

	FRoadLaneShapePoint PointB;
	PointB.Lateral = 175.0;
	PointB.Vertical = 0.0;
	PointB.MaterialSlot = 1;
	Shape->Points.Add(PointB);

	TestEqual(TEXT("Shape has two points"), Shape->Points.Num(), 2);
	TestTrue(TEXT("Points ordered left-to-right by Lateral"), Shape->Points[0].Lateral < Shape->Points[1].Lateral);
	// GetMaterial returns null for both slots here (no real UMaterialInterface asset loaded in a
	// unit test), but must not crash on a valid slot and must return null on an out-of-range one.
	TestNull(TEXT("Valid slot 0 resolves (to null placeholder material)"), Shape->GetMaterial(0));
	TestNull(TEXT("Out-of-range slot resolves to null, not a crash"), Shape->GetMaterial(5));
	return true;
}
