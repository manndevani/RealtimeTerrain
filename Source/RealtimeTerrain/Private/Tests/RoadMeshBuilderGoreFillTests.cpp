// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Algo/Reverse.h"

// Verifies RoadMesh::AppendGoreFill's self-correcting winding: every triangle it appends must
// face +Z regardless of which direction the caller's chain happens to run in. This is the single
// most safety-critical property of this function -- this project has previously shipped a green
// test suite that hid an inside-out junction surface (see memory: "verify geometry claims with
// tests"), so this checks the actual FDynamicMesh3-computed normal (GetTriNormal), the real
// downstream contract, not just that the function runs without crashing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderGoreFillWindingTest, "TerraDyne.RoadMeshBuilder.GoreFillAlwaysFacesUp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadMeshBuilderGoreFillWindingTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;

	const FVector Apex(0.0, 0.0, 100.0);
	TArray<FVector> ForwardChain = {
		FVector(500.0, -200.0, 100.0),
		FVector(600.0, 0.0, 100.0),
		FVector(500.0, 200.0, 100.0),
	};
	TArray<FVector> ReversedChain = ForwardChain;
	Algo::Reverse(ReversedChain);

	auto CheckAllTrianglesFaceUp = [this](const TArray<FVector>& Chain, const TCHAR* CaseLabel) -> bool
	{
		FDynamicMesh3 Mesh;
		RoadMesh::AppendGoreFill(Mesh, FVector(0.0, 0.0, 100.0), Chain, RoadMesh::Slots::Curb, FTransform::Identity, 0.002f);

		const int32 ExpectedTriangleCount = Chain.Num() - 1;
		TestEqual(FString::Printf(TEXT("%s: triangle count matches chain segments"), CaseLabel), Mesh.TriangleCount(), ExpectedTriangleCount);
		if (Mesh.TriangleCount() != ExpectedTriangleCount)
		{
			return false;
		}

		bool bAllFaceUp = true;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			const FVector3d Normal = Mesh.GetTriNormal(Tid);
			if (Normal.Z <= 0.0)
			{
				bAllFaceUp = false;
			}
			AddInfo(FString::Printf(TEXT("%s triangle %d normal: (%.3f, %.3f, %.3f)"), CaseLabel, Tid, Normal.X, Normal.Y, Normal.Z));
		}
		TestTrue(FString::Printf(TEXT("%s: every gore-fill triangle faces +Z (up)"), CaseLabel), bAllFaceUp);
		return bAllFaceUp;
	};

	const bool bForwardOk = CheckAllTrianglesFaceUp(ForwardChain, TEXT("Forward-ordered chain"));
	const bool bReversedOk = CheckAllTrianglesFaceUp(ReversedChain, TEXT("Reversed-ordered chain"));

	return bForwardOk && bReversedOk;
}
