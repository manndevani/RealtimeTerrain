// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderMarkingsTest, "TerraDyne.RoadMeshBuilder.MarkingsTwoLane", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadMeshBuilderMarkingsTest::RunTest(const FString& Parameters)
{
	UE::Geometry::FDynamicMesh3 Mesh;
	RoadMesh::PrepareMesh(Mesh);

	TArray<RoadMesh::FFrame> Frames;
	const int32 NumStations = 10;
	const double StationSpacing = 100.0; // 1m per station, total 9m
	for (int32 i = 0; i < NumStations; ++i)
	{
		RoadMesh::FFrame F;
		F.Origin = FVector(i * StationSpacing, 0.0, 0.0);
		F.Tangent = FVector(1.0, 0.0, 0.0);
		F.Right = FVector(0.0, 1.0, 0.0);
		F.Up = FVector(0.0, 0.0, 1.0);
		F.DistanceAlong = i * StationSpacing;
		Frames.Add(F);
	}

	// 2-lane road, 700 cm total width, symmetric (350 cm each side of the centreline).
	RoadMesh::BuildRoadMarkings(Mesh, Frames, 350.0, 350.0, 2, true, FTransform::Identity, 0.002f);

	TestTrue(TEXT("Marking triangles were generated"), Mesh.TriangleCount() > 0);
	TestTrue(TEXT("Marking vertices were generated"), Mesh.VertexCount() > 0);

	// Check that we have triangles using MarkingWhite (slot 3) and MarkingYellow (slot 4)
	bool bHasWhiteMarking = false;
	bool bHasYellowMarking = false;
	const UE::Geometry::FDynamicMeshMaterialAttribute* MatAttr = Mesh.Attributes()->GetMaterialID();
	TestNotNull(TEXT("Material attribute exists"), MatAttr);

	if (MatAttr)
	{
		for (int32 Tid : Mesh.TriangleIndicesItr())
		{
			int32 MatID = MatAttr->GetValue(Tid);
			if (MatID == RoadMesh::Slots::MarkingWhite)
			{
				bHasWhiteMarking = true;
			}
			else if (MatID == RoadMesh::Slots::MarkingYellow)
			{
				bHasYellowMarking = true;
			}
		}
	}

	TestTrue(TEXT("Generated solid white edge markings"), bHasWhiteMarking);
	TestTrue(TEXT("Generated yellow centerline markings"), bHasYellowMarking);

	return true;
}

// -----------------------------------------------------------------------------
// Regression test: an asymmetric driving surface (a sidewalk or extra lane on only one side --
// LeftLanes and RightLanes are independent lists, so the two are not generally equal) must place
// its edge markings at the real left/right edges, not at +/- some width averaged/assumed symmetric
// about the centreline. BuildRoadMarkings used to take one shared RoadWidth and derive a single
// symmetric HalfWidth from it; for an asymmetric style that placed the left edge line beyond the
// real surface on one side and short of it on the other. This checks the actual downstream
// contract -- measured world-space vertex positions -- not just "did it generate some triangles".
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderAsymmetricMarkingsTest, "TerraDyne.RoadMeshBuilder.MarkingsAsymmetric", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadMeshBuilderAsymmetricMarkingsTest::RunTest(const FString& Parameters)
{
	UE::Geometry::FDynamicMesh3 Mesh;
	RoadMesh::PrepareMesh(Mesh);

	TArray<RoadMesh::FFrame> Frames;
	for (int32 i = 0; i < 10; ++i)
	{
		RoadMesh::FFrame F;
		F.Origin = FVector(i * 100.0, 0.0, 0.0);
		F.Tangent = FVector(1.0, 0.0, 0.0);
		F.Right = FVector(0.0, 1.0, 0.0);
		F.Up = FVector(0.0, 0.0, 1.0);
		F.DistanceAlong = i * 100.0;
		Frames.Add(F);
	}

	// Deliberately lopsided: 200cm on the left, 600cm on the right -- the shape a sidewalk-only
	// on one side, or one extra lane, produces. bHasCurbs=true so the drivable width is used as-is
	// (no 0.96 shrink), keeping the expected numbers exact.
	constexpr double LeftHalfWidth = 200.0;
	constexpr double RightHalfWidth = 600.0;
	RoadMesh::BuildRoadMarkings(Mesh, Frames, LeftHalfWidth, RightHalfWidth, 2, /*bHasCurbs*/ true, FTransform::Identity, 0.002f);

	TestTrue(TEXT("Marking triangles were generated"), Mesh.TriangleCount() > 0);
	if (Mesh.TriangleCount() == 0) return false;

	double MinY = TNumericLimits<double>::Max();
	double MaxY = TNumericLimits<double>::Lowest();
	for (int32 Vid : Mesh.VertexIndicesItr())
	{
		const FVector3d V = Mesh.GetVertex(Vid);
		MinY = FMath::Min(MinY, V.Y);
		MaxY = FMath::Max(MaxY, V.Y);
	}

	// EdgeOffsetLeft = -LeftHalfWidth + 20, EdgeOffsetRight = RightHalfWidth - 20, each +/- half the
	// 15cm marking width -- see BuildRoadMarkings. Left: -180 +/- 7.5. Right: 580 +/- 7.5.
	TestTrue(FString::Printf(TEXT("Left edge marking (min Y = %f) sits at the real left edge, not a symmetric guess"), MinY),
		FMath::Abs(MinY - (-187.5)) < 5.0);
	TestTrue(FString::Printf(TEXT("Right edge marking (max Y = %f) sits at the real right edge, not a symmetric guess"), MaxY),
		FMath::Abs(MaxY - 587.5) < 5.0);

	// The two sides must actually differ -- a residual symmetric-collapse bug would put both edges
	// at the same distance from 0 regardless of the asymmetric inputs.
	TestTrue(TEXT("The two edges are NOT equidistant from the centreline -- proving the asymmetry survived"),
		FMath::Abs(FMath::Abs(MinY) - FMath::Abs(MaxY)) > 100.0);

	return true;
}
