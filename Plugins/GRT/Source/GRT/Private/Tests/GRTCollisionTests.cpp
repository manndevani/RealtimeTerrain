// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Physics/GRTCollision.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// Regression test for a real bug: IsLocationTraceable assumed the mesh it's given has a top surface
// AND a bottom slab (TopVerts = VertexCount() / 2), but the only mesh generator that actually builds
// this kind of grid (AGRTChunk::RebuildMesh) explicitly builds a single top surface with no
// bottom slab -- confirmed by that function's own comment. Dividing by 2 anyway under-computes the
// grid resolution by a factor of sqrt(2), which is NOT a safe no-op: the resulting (too-small)
// ExpectedTopTris estimate stayed comfortably under the mesh's real triangle count, so the "layout
// mismatch, assume traceable" bail-out never caught it either -- the corrupted TriBase silently
// checked an unrelated cell's MaterialID instead of the one actually queried. This builds a small
// grid mesh matching RebuildMesh's exact vertex/triangle layout, punches one real hole at a known
// cell, and confirms IsLocationTraceable finds it at exactly that cell and nowhere else.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTCollisionLocationTraceableTest, "GRT.Physics.IsLocationTraceableFindsTheRightCell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGRTCollisionLocationTraceableTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World exists"), World);
	if (!World) return false;

	AActor* Owner = World->SpawnActor<AActor>();
	TestNotNull(TEXT("Owner actor spawned"), Owner);
	if (!Owner) return false;

	UDynamicMeshComponent* MeshComp = NewObject<UDynamicMeshComponent>(Owner);
	MeshComp->RegisterComponent();
	TestNotNull(TEXT("Mesh component created"), MeshComp);
	if (!MeshComp || !MeshComp->GetDynamicMesh()) return false;

	// Same shape as AGRTChunk::RebuildMesh: a single top-surface grid, Resolution x Resolution
	// vertices, centered on the origin, Resolution=5 / WorldSize=400 -> Step=100, cells span
	// [-200,-100,0,100,200] on each axis.
	constexpr int32 Resolution = 5;
	constexpr double WorldSize = 400.0;
	constexpr double Step = WorldSize / (Resolution - 1);
	constexpr double Offset = WorldSize * -0.5;

	MeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		Mesh.Clear();
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnableMaterialID();

		for (int32 Y = 0; Y < Resolution; Y++)
		{
			for (int32 X = 0; X < Resolution; X++)
			{
				Mesh.AppendVertex(FVector3d(Offset + X * Step, Offset + Y * Step, 0.0));
			}
		}

		for (int32 Y = 0; Y < Resolution - 1; Y++)
		{
			for (int32 X = 0; X < Resolution - 1; X++)
			{
				const int32 BL = Y * Resolution + X;
				const int32 BR = BL + 1;
				const int32 TL = (Y + 1) * Resolution + X;
				const int32 TR = TL + 1;
				Mesh.AppendTriangle(BL, TR, BR);
				Mesh.AppendTriangle(BL, TL, TR);
			}
		}

		// Punch a hole at cell (CellX=2, CellY=2) -- world X/Y in [0,100], centered at (50,50).
		// TriBase = (CellY * (Resolution-1) + CellX) * 2 = (2*4 + 2) * 2 = 20.
		UE::Geometry::FDynamicMeshMaterialAttribute* MatAttrib = Mesh.Attributes()->GetMaterialID();
		MatAttrib->SetValue(20, 1);
		MatAttrib->SetValue(21, 1);
	});

	int32 ActualVertexCount = 0;
	MeshComp->GetDynamicMesh()->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh) { ActualVertexCount = Mesh.VertexCount(); });
	TestEqual(TEXT("Grid has the expected 25 vertices (single surface, no bottom slab)"), ActualVertexCount, 25);

	// The holed cell itself: not traceable.
	TestFalse(TEXT("The holed cell (50,50) is correctly reported not traceable"),
		UGRTCollisionLib::IsLocationTraceable(MeshComp, FVector(50.0, 50.0, 0.0)));

	// An adjacent, unmodified cell: still traceable -- proves the fix narrows down to exactly the
	// right cell instead of either flagging everything or nothing.
	TestTrue(TEXT("An adjacent, unmodified cell (-150,-150) is still traceable"),
		UGRTCollisionLib::IsLocationTraceable(MeshComp, FVector(-150.0, -150.0, 0.0)));

	// A cell diagonally opposite the hole, also unmodified: still traceable. NOT world (0,0,0) --
	// with an even cell count per axis (Resolution=5 -> 4 cells), (0,0) sits exactly on the shared
	// corner of cells (1,1)/(1,2)/(2,1)/(2,2), and IsLocationTraceable's Floor-based cell mapping
	// resolves that boundary into CellX=CellY=2 -- the SAME cell the hole above is punched into.
	TestTrue(TEXT("A cell diagonally opposite the hole is traceable"),
		UGRTCollisionLib::IsLocationTraceable(MeshComp, FVector(-150.0, 150.0, 0.0)));

	return true;
}
#endif // WITH_EDITOR
