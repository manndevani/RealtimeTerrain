// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"

// Verifies BuildRoadSectionFromStyle composes a flat, unshaped two-lane style (one driving lane
// each side) into a section matching BuildRoadSection's own top-chain-left-to-right,
// underside-right-to-left convention. The lane boundary sits at Lateral 0 exactly once per side
// of the seam -- two coincident vertices there (the outgoing edge of the left lane and the
// incoming edge of the right lane), not a defect: a real boundary vertex has to exist there for
// lane markings to attach to later, even when both lanes are flat and it's not visually distinct.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderStyleFlatTest, "TerraDyne.RoadMeshBuilder.StyleFlatTwoLane", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadMeshBuilderStyleFlatTest::RunTest(const FString& Parameters)
{
	URoadStylePreset* Style = NewObject<URoadStylePreset>();
	FRoadStyleLaneEntry Left;
	Left.Width = 350.0;
	Left.LaneType = ERealtimeLaneType::Driving;
	Style->LeftLanes.Add(Left);
	FRoadStyleLaneEntry Right;
	Right.Width = 350.0;
	Right.LaneType = ERealtimeLaneType::Driving;
	Style->RightLanes.Add(Right);

	RoadMesh::FSection Section;
	RoadMesh::BuildRoadSectionFromStyle(*Style, 25.0, Section);

	TestEqual(TEXT("Section vertex count"), Section.Num(), 6);
	TestEqual(TEXT("Far-left top vertex"), Section[0].Lateral, -350.0, 0.01);
	TestEqual(TEXT("Left lane's far edge sits at 0"), Section[1].Lateral, 0.0, 0.01);
	TestEqual(TEXT("Right lane's near edge also sits at 0"), Section[2].Lateral, 0.0, 0.01);
	TestEqual(TEXT("Far-right top vertex"), Section[3].Lateral, 350.0, 0.01);
	TestEqual(TEXT("Every top vertex is flat"), Section[0].Vertical, 0.0, 0.01);
	TestEqual(TEXT("Every top vertex is flat"), Section[3].Vertical, 0.0, 0.01);
	TestEqual(TEXT("Underside starts at the right edge"), Section[4].Lateral, 350.0, 0.01);
	TestEqual(TEXT("Underside closes at the left edge"), Section[5].Lateral, -350.0, 0.01);
	TestTrue(TEXT("Underside sits below the slab"), Section[4].Vertical < 0.0);
	// Only the top chain (indices 0-3) should read as driving surface; the underside (4-5) is
	// always Flank material regardless of lane style, matching BuildRoadSection's own convention
	// -- the underside is never seen, so it doesn't need lane-level material fidelity.
	for (int32 i = 0; i < 4; i++)
	{
		TestEqual(TEXT("Top chain vertex reads as driving surface (no sidewalk in this style)"), Section[i].EdgeMaterialID, RoadMesh::Slots::Surface);
	}
	TestEqual(TEXT("Underside reads as Flank material"), Section[4].EdgeMaterialID, RoadMesh::Slots::Flank);
	TestEqual(TEXT("Underside reads as Flank material"), Section[5].EdgeMaterialID, RoadMesh::Slots::Flank);
	return true;
}

// Verifies a lane's Shape rescales its own authored span to the lane's actual width while leaving
// heights absolute -- a shape authored once (e.g. a kerb rising 15cm over a nominal 200cm) reads
// correctly whether the lane using it is 200cm or 300cm wide.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderStyleShapeRescaleTest, "TerraDyne.RoadMeshBuilder.StyleShapeRescale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadMeshBuilderStyleShapeRescaleTest::RunTest(const FString& Parameters)
{
	URoadLaneShape* Shape = NewObject<URoadLaneShape>();
	FRoadLaneShapePoint P0;
	P0.Lateral = 0.0;
	P0.Vertical = 0.0;
	Shape->Points.Add(P0);
	FRoadLaneShapePoint P1;
	P1.Lateral = 200.0;
	P1.Vertical = 15.0;
	Shape->Points.Add(P1);

	URoadStylePreset* Style = NewObject<URoadStylePreset>();
	FRoadStyleLaneEntry Right;
	Right.Width = 300.0;
	Right.LaneType = ERealtimeLaneType::Sidewalk;
	Right.Shape = Shape;
	Style->RightLanes.Add(Right);

	RoadMesh::FSection Section;
	RoadMesh::BuildRoadSectionFromStyle(*Style, 25.0, Section);

	// No left lanes, so the shaped right lane's span starts at Lateral 0.
	TestEqual(TEXT("Section has the shape's 2 points plus 2 underside points"), Section.Num(), 4);
	TestEqual(TEXT("Shape start rescaled to lane start"), Section[0].Lateral, 0.0, 0.01);
	TestEqual(TEXT("Shape end rescaled to the lane's actual 300cm width, not the shape's authored 200cm"), Section[1].Lateral, 300.0, 0.01);
	TestEqual(TEXT("Height stays absolute across the rescale"), Section[1].Vertical, 15.0, 0.01);
	TestEqual(TEXT("Sidewalk lane reads as footpath material"), Section[0].EdgeMaterialID, RoadMesh::Slots::Footpath);
	return true;
}
