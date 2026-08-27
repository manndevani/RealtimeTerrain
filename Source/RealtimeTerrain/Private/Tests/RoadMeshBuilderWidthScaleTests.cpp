// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadMeshBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"

// FSweepOptions::WidthScalePerFrame (RoadWidthProfile.h's gizmo-Scale-mode feature) must scale EVERY
// section vertex, not just the outer band beyond BandInnerHalfWidth the way BandScalePerFrame does --
// this is the property that gives it a genuinely different effect from band-scaling, not a redundant
// second knob doing the same thing. Both options are exercised together here (not in isolation) to
// pin down the documented composition order: width-scale is applied to Lateral FIRST, and
// BandInnerHalfWidth's own threshold check runs against the already-widened value afterwards -- an
// inner vertex widened just enough to cross that threshold should start picking up band narrowing too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadMeshBuilderWidthScaleTest, "TerraDyne.RoadMesh.WidthScaleAppliesToWholeSection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadMeshBuilderWidthScaleTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;

	// A straight run, so every frame shares the same Right/Up -- keeps the expected Lateral algebra
	// simple to hand-verify.
	const TArray<FVector> Points = { FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0) };
	TArray<RoadMesh::FFrame> Frames;
	TestTrue(TEXT("BuildFramesFromPolyline should succeed for a straight run"), RoadMesh::BuildFramesFromPolyline(Points, Frames));
	TestEqual(TEXT("One frame per point"), Frames.Num(), 2);
	if (Frames.Num() != 2) return false;

	// Outer-left, inner-left, inner-right, outer-right -- inner sits well inside BandInnerHalfWidth
	// (150), outer well outside it, at every frame's UNSCALED authored width.
	RoadMesh::FSection Section;
	Section.Add(RoadMesh::FSectionVertex(-400.0, 0.0, RoadMesh::Slots::Surface));
	Section.Add(RoadMesh::FSectionVertex(-50.0, 0.0, RoadMesh::Slots::Surface));
	Section.Add(RoadMesh::FSectionVertex(50.0, 0.0, RoadMesh::Slots::Surface));
	Section.Add(RoadMesh::FSectionVertex(400.0, 0.0, RoadMesh::Slots::Surface));

	RoadMesh::FSweepOptions Options;
	Options.bSectionIsClosed = false;
	Options.BandInnerHalfWidth = 150.0;
	// Frame 0: unscaled (a no-op sanity baseline). Frame 1: width doubled AND the outer band halved,
	// so the two effects have to be told apart, not accidentally both no-ops or both full-strength.
	Options.BandScalePerFrame = { 1.0f, 0.5f };
	Options.WidthScalePerFrame = { 1.0f, 2.0f };

	FDynamicMesh3 Mesh;
	RoadMesh::SweepSection(Mesh, Section, Frames, FTransform::Identity, Options);

	const int32 NumSection = Section.Num();
	TestEqual(TEXT("One vertex per (frame, section) pair"), Mesh.VertexCount(), Frames.Num() * NumSection);

	auto LateralOf = [&Mesh](int32 FrameIndex, int32 SectionIndex, const RoadMesh::FFrame& Frame, int32 InNumSection) -> double
	{
		const FVector3d Pos = Mesh.GetVertex(FrameIndex * InNumSection + SectionIndex);
		return FVector::DotProduct(FVector(Pos) - Frame.Origin, Frame.Right);
	};

	// Frame 0 -- WidthScale 1.0, BandScale 1.0: every vertex lands exactly at its authored Lateral,
	// the band-narrowing math being a full no-op at scale 1.0. Confirms this option set reproduces
	// unmodified behavior before checking what frame 1 actually changes.
	TestEqual(TEXT("Frame 0 outer-left unchanged"), LateralOf(0, 0, Frames[0], NumSection), -400.0, 0.5);
	TestEqual(TEXT("Frame 0 inner-left unchanged"), LateralOf(0, 1, Frames[0], NumSection), -50.0, 0.5);
	TestEqual(TEXT("Frame 0 inner-right unchanged"), LateralOf(0, 2, Frames[0], NumSection), 50.0, 0.5);
	TestEqual(TEXT("Frame 0 outer-right unchanged"), LateralOf(0, 3, Frames[0], NumSection), 400.0, 0.5);

	// Frame 1, inner vertices: |50| * WidthScale(2.0) = 100, still under BandInnerHalfWidth (150) --
	// band-narrowing never engages, so width-scale is the ONLY effect visible here. This is the
	// property BandScalePerFrame alone could never produce, since 50 never crosses its own threshold
	// regardless of BandScale.
	TestEqual(TEXT("Frame 1 inner-left widened, no band narrowing (|100| < 150)"), LateralOf(1, 1, Frames[1], NumSection), -100.0, 0.5);
	TestEqual(TEXT("Frame 1 inner-right widened, no band narrowing (|100| < 150)"), LateralOf(1, 2, Frames[1], NumSection), 100.0, 0.5);

	// Frame 1, outer vertices: |400| * 2.0 = 800 first, THEN band narrowing applies to the portion
	// beyond BandInnerHalfWidth: 150 + (800 - 150) * 0.5 = 475.
	TestEqual(TEXT("Frame 1 outer-left widened then band-narrowed"), LateralOf(1, 0, Frames[1], NumSection), -475.0, 0.5);
	TestEqual(TEXT("Frame 1 outer-right widened then band-narrowed"), LateralOf(1, 3, Frames[1], NumSection), 475.0, 0.5);

	return true;
}
