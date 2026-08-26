// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RealtimeRoadSplineActor.h"
#include "RoadMeshBuilder.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// End-to-end check that setting StylePreset on a real spawned road actor actually routes mesh
// generation through BuildRoadSectionFromStyle instead of the CrossSectionSettings-driven path --
// not just that the two functions individually produce correct sections (already covered by
// TerraDyne.RoadMeshBuilder.Style*), but that the actor's own GenerateRoadMesh branch picks the
// right one and the resulting mesh is actually as wide as the style says, not the 800cm
// CrossSectionSettings.RoadWidth default a style-preset road ignores for its own geometry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSplineActorStyleWidthTest, "TerraDyne.RoadSplineActor.StylePresetDrivesWidth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSplineActorStyleWidthTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	URoadStylePreset* Style = NewObject<URoadStylePreset>();
	FRoadStyleLaneEntry Left;
	Left.Width = 300.0;
	Left.LaneType = ERealtimeLaneType::Driving;
	Style->LeftLanes.Add(Left);
	FRoadStyleLaneEntry Right;
	Right.Width = 300.0;
	Right.LaneType = ERealtimeLaneType::Driving;
	Style->RightLanes.Add(Right);
	// Total style width (600) is deliberately far from CrossSectionSettings' own 800cm default so
	// the two paths can't be confused for one another by coincidence.

	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>();
	TestNotNull(TEXT("Road actor should spawn"), Road);
	if (!Road) return false;

	Road->bConformToTerrainInRealtime = false; // use the spline's own Z; no ground needed for this check.
	Road->bRebaseTerrain = false;              // no TerraDyne terrain in this world to rebase into.
	Road->StylePreset = Style;

	Road->SetPointsRealtime({
		FVector(-2000.0, 0.0, 0.0),
		FVector(0.0, 0.0, 0.0),
		FVector(2000.0, 0.0, 0.0),
	});
	Road->RequestRebuild();

	UDynamicMeshComponent* RoadMeshComp = Road->RoadMeshComponent;
	TestNotNull(TEXT("Road mesh component should exist"), RoadMeshComp);
	if (!RoadMeshComp) return false;

	const UE::Geometry::FDynamicMesh3* Mesh = RoadMeshComp->GetMesh();
	TestNotNull(TEXT("Road should have generated mesh geometry"), Mesh);
	if (!Mesh || Mesh->VertexCount() == 0) return false;

	double MinY = TNumericLimits<double>::Max();
	double MaxY = TNumericLimits<double>::Lowest();
	for (int32 VidInt : Mesh->VertexIndicesItr())
	{
		const FVector3d V = Mesh->GetVertex(VidInt);
		MinY = FMath::Min(MinY, V.Y);
		MaxY = FMath::Max(MaxY, V.Y);
	}
	const double MeasuredWidth = MaxY - MinY;

	// Style's total width is 600; CrossSectionSettings.RoadWidth defaults to 800. A wide tolerance
	// (not exact-to-the-mm) because SweepSection's frames aren't guaranteed axis-aligned at every
	// station, but tight enough that landing near 800 (the un-styled default) would still fail it.
	TestTrue(
		FString::Printf(TEXT("Mesh width (%f) should match the style's 600cm, not CrossSectionSettings' 800cm default"), MeasuredWidth),
		FMath::Abs(MeasuredWidth - 600.0) < 50.0);
	return true;
}
#endif // WITH_EDITOR
