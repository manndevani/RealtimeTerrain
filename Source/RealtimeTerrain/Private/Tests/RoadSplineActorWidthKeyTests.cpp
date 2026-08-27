// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RealtimeRoadSplineActor.h"
#include "Components/SplineComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// End-to-end: SetWidthMultiplierAtDistance (the gizmo Scale-mode write path) on a real spawned road
// actually widens the generated mesh near that distance and leaves the rest of the road untouched --
// not just that RoadWidthProfile::EvaluateWidth or SweepSection's WidthScalePerFrame are correct in
// isolation (covered by TerraDyne.RoadWidthProfile.WidthMath and
// TerraDyne.RoadMesh.WidthScaleAppliesToWholeSection), but that the actor actually wires
// WidthKeys -> per-run distance rebasing -> Options.WidthScalePerFrame -> the real mesh, matching the
// same standard TerraDyne.RoadSplineActor.StylePresetDrivesWidth already holds for style-driven width.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSplineActorWidthKeyTest, "TerraDyne.RoadSplineActor.WidthKeyWidensMeshAtItsOwnDistance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadSplineActorWidthKeyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("World must be valid"), World);
	if (!World) return false;

	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>();
	TestNotNull(TEXT("Road actor should spawn"), Road);
	if (!Road) return false;

	Road->bConformToTerrainInRealtime = false; // use the spline's own Z; no ground needed for this check.
	Road->bRebaseTerrain = false;              // no TerraDyne terrain in this world to rebase into.

	// A straight, unsplit (no junctions) run -- Run.StartDistance is exactly 0, so a key's Dist maps
	// directly onto Frame::DistanceAlong with no rebasing to reason about.
	Road->SetPointsRealtime({
		FVector(-2000.0, 0.0, 0.0),
		FVector(0.0, 0.0, 0.0),
		FVector(2000.0, 0.0, 0.0),
	});
	Road->RequestRebuild();

	auto MeasureWidthNearX = [Road](double TargetX, double Tolerance) -> double
	{
		const UE::Geometry::FDynamicMesh3* Mesh = Road->RoadMeshComponent ? Road->RoadMeshComponent->GetMesh() : nullptr;
		if (!Mesh) return -1.0;
		// SweepSection appends vertices in the mesh COMPONENT's local space (WorldToLocal), not
		// world space -- TargetX is a world-space station along the road, so every vertex has to be
		// transformed back to world space before comparing, or this silently compares unrelated
		// coordinate spaces whenever the component's transform isn't exactly identity.
		const FTransform LocalToWorld = Road->RoadMeshComponent->GetComponentTransform();
		double MinY = TNumericLimits<double>::Max();
		double MaxY = TNumericLimits<double>::Lowest();
		bool bAny = false;
		for (int32 VidInt : Mesh->VertexIndicesItr())
		{
			const FVector V = LocalToWorld.TransformPosition(FVector(Mesh->GetVertex(VidInt)));
			if (FMath::Abs(V.X - TargetX) <= Tolerance)
			{
				MinY = FMath::Min(MinY, V.Y);
				MaxY = FMath::Max(MaxY, V.Y);
				bAny = true;
			}
		}
		return bAny ? (MaxY - MinY) : -1.0;
	};

	// Wide enough to reliably straddle the road's own resampling grid (confirmed empirically at
	// roughly 150cm apart for this length of road) regardless of exactly where a sample happens to
	// land relative to a queried X -- tight enough that "near the midpoint" and "near the start"
	// still can't be confused with each other on a 4000cm road.
	constexpr double SampleTolerance = 100.0;

	const double BaselineWidthAtMid = MeasureWidthNearX(0.0, SampleTolerance);
	const double BaselineWidthAtEnd = MeasureWidthNearX(-1900.0, SampleTolerance);
	TestTrue(TEXT("Baseline: found vertices near the midpoint"), BaselineWidthAtMid > 0.0);
	TestTrue(TEXT("Baseline: found vertices near the start"), BaselineWidthAtEnd > 0.0);
	AddInfo(FString::Printf(TEXT("Baseline width -- mid: %.1f, near-start: %.1f"), BaselineWidthAtMid, BaselineWidthAtEnd));
	if (BaselineWidthAtMid <= 0.0 || BaselineWidthAtEnd <= 0.0) return false;

	const double DistAtMidpoint = Road->SplineComponent->GetDistanceAlongSplineAtSplinePoint(1);
	TestEqual(TEXT("The middle control point's distance-along-spline is halfway along the 4000cm run"), DistAtMidpoint, 2000.0, 1.0);

	// A single key: TerraDyne.RoadWidthProfile.WidthMath already pins down that this blends smoothly
	// to 1.0 within a 500cm radius -- -1900 (1900cm away) sits well outside it, so that end of the
	// road is the "should stay untouched" control for this test.
	Road->SetWidthMultiplierAtDistance(DistAtMidpoint, 2.0);

	const double WidenedWidthAtMid = MeasureWidthNearX(0.0, SampleTolerance);
	const double WidenedWidthAtEnd = MeasureWidthNearX(-1900.0, SampleTolerance);
	AddInfo(FString::Printf(TEXT("After a 2x width key at the midpoint -- mid: %.1f, near-start: %.1f"), WidenedWidthAtMid, WidenedWidthAtEnd));

	TestTrue(
		FString::Printf(TEXT("Width at the key's own distance should have roughly doubled (%.1f -> %.1f)"), BaselineWidthAtMid, WidenedWidthAtMid),
		WidenedWidthAtMid > BaselineWidthAtMid * 1.8);
	TestTrue(
		FString::Printf(TEXT("Width far from the key (outside its falloff radius) should be essentially unchanged (%.1f -> %.1f)"), BaselineWidthAtEnd, WidenedWidthAtEnd),
		FMath::Abs(WidenedWidthAtEnd - BaselineWidthAtEnd) < 20.0);

	TestEqual(TEXT("GetWidthMultiplierAtDistance round-trips exactly what was just written"), Road->GetWidthMultiplierAtDistance(DistAtMidpoint), 2.0, 0.01);

	// Writing again at (nearly) the same distance updates the existing key in place rather than
	// accumulating a second one -- the gizmo calls this every frame of a drag, and a road held under
	// a slowly-adjusted Scale handle for even a few seconds would otherwise leave dozens of
	// near-duplicate keys behind.
	Road->SetWidthMultiplierAtDistance(DistAtMidpoint + 0.1, 2.5);
	TestEqual(TEXT("Re-writing near an existing key updates it in place, not a second entry"), Road->WidthKeys.Num(), 1);
	TestEqual(TEXT("The in-place update took the new value"), Road->GetWidthMultiplierAtDistance(DistAtMidpoint), 2.5, 0.01);

	return true;
}
#endif // WITH_EDITOR
