// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "IO/GRTOSMIngest.h"
#include "IO/GRTOSMParser.h"
#include "Algo/Reverse.h"

#if WITH_EDITOR

// -----------------------------------------------------------------------------
// Winding. AGRTBuildingActor::ResetToDefaultFootprint winds counter-clockwise on purpose,
// and wall generation plus the terrain pad both read that ordering -- a clockwise footprint builds
// the building inside-out. OSM's own ring convention cannot settle this either way, because this
// project georeferences through Cesium in East-South-Up: latitude grows toward -Y, so the
// conversion flips every ring's handedness on the way in.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTOSMWindingTest, "GRT.OSM.FootprintWinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTOSMWindingTest::RunTest(const FString& Parameters)
{
	// Exactly the ordering ResetToDefaultFootprint produces, at 100x100.
	const TArray<FVector> CounterClockwise = {
		FVector(-50, -50, 0), FVector(50, -50, 0), FVector(50, 50, 0), FVector(-50, 50, 0)
	};
	TArray<FVector> Clockwise = CounterClockwise;
	Algo::Reverse(Clockwise);

	const double CCWArea = FGRTOSMIngest::ComputeSignedArea2D(CounterClockwise);
	const double CWArea = FGRTOSMIngest::ComputeSignedArea2D(Clockwise);

	TestTrue(TEXT("The default footprint's ordering has positive signed area"), CCWArea > 0.0);
	TestTrue(TEXT("Reversing it flips the sign"), CWArea < 0.0);
	TestTrue(TEXT("Magnitude is the real area, 100x100"), FMath::IsNearlyEqual(FMath::Abs(CCWArea), 10000.0, 0.01));

	// Normalising must fix a clockwise ring and leave a counter-clockwise one untouched.
	TArray<FVector> ToFix = Clockwise;
	FGRTOSMIngest::NormalizeFootprintWinding(ToFix);
	TestTrue(TEXT("A clockwise footprint is reversed"), FGRTOSMIngest::ComputeSignedArea2D(ToFix) > 0.0);

	TArray<FVector> AlreadyGood = CounterClockwise;
	FGRTOSMIngest::NormalizeFootprintWinding(AlreadyGood);
	TestTrue(TEXT("A counter-clockwise footprint is left alone"),
		FGRTOSMIngest::ComputeSignedArea2D(AlreadyGood) > 0.0);
	for (int32 i = 0; i < AlreadyGood.Num(); ++i)
	{
		TestTrue(TEXT("...with its point order genuinely unchanged"), AlreadyGood[i].Equals(CounterClockwise[i], 0.01));
	}

	// Degenerate input must not divide by zero or assert.
	TestEqual(TEXT("A two-point ring has no area"),
		FGRTOSMIngest::ComputeSignedArea2D({ FVector::ZeroVector, FVector(10, 0, 0) }), 0.0);
	TestEqual(TEXT("An empty ring has no area"), FGRTOSMIngest::ComputeSignedArea2D({}), 0.0);

	return true;
}

// -----------------------------------------------------------------------------
// Footprint construction: closing node, centroid origin, local space, winding.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTOSMFootprintTest, "GRT.OSM.FootprintBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTOSMFootprintTest::RunTest(const FString& Parameters)
{
	// A square offset well away from the origin, closed the way OSM closes rings: first node
	// repeated last. Wound clockwise, so normalisation has real work to do.
	const TArray<FVector> WorldRing = {
		FVector(1000, 2000, 0),
		FVector(1000, 2200, 0),
		FVector(1200, 2200, 0),
		FVector(1200, 2000, 0),
		FVector(1000, 2000, 0)   // repeated closing node
	};

	FVector Origin = FVector::ZeroVector;
	TArray<FVector> Local = FGRTOSMIngest::BuildFootprintLocal(WorldRing, Origin);

	TestEqual(TEXT("The repeated closing node is dropped, leaving four corners"), Local.Num(), 4);
	TestTrue(TEXT("Origin sits on the footprint centroid"), Origin.Equals(FVector(1100, 2100, 0), 0.01));
	TestTrue(TEXT("Footprint is normalised counter-clockwise for the wall generator"),
		FGRTOSMIngest::ComputeSignedArea2D(Local) > 0.0);

	// Local + origin must reconstruct the original corners: this is what makes moving a building
	// one transform rather than a per-point rewrite, and it is what the spline actually stores.
	TestTrue(TEXT("Local points are flattened to Z=0"),
		Local.ContainsByPredicate([](const FVector& P) { return P.Z == 0.0; }) && !Local.ContainsByPredicate([](const FVector& P) { return P.Z != 0.0; }));

	double AreaWorld = 0.0;
	{
		TArray<FVector> Reconstructed;
		for (const FVector& P : Local)
		{
			Reconstructed.Add(P + Origin);
		}
		AreaWorld = FMath::Abs(FGRTOSMIngest::ComputeSignedArea2D(Reconstructed));
	}
	TestTrue(TEXT("Reconstructed world footprint keeps the original 200x200 area"),
		FMath::IsNearlyEqual(AreaWorld, 40000.0, 1.0));

	// Too little to be a polygon.
	FVector Unused;
	TestEqual(TEXT("A two-point way yields no footprint"),
		FGRTOSMIngest::BuildFootprintLocal({ FVector::ZeroVector, FVector(10, 0, 0) }, Unused).Num(), 0);
	TestEqual(TEXT("A ring of coincident points yields no footprint"),
		FGRTOSMIngest::BuildFootprintLocal(
			{ FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector }, Unused).Num(), 0);

	return true;
}

// -----------------------------------------------------------------------------
// Node resolution.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGRTOSMCentrelineTest, "GRT.OSM.Centreline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGRTOSMCentrelineTest::RunTest(const FString& Parameters)
{
	// Node resolution against a real parsed document.
	const FGRTOSMData Data = FGRTOSMParser::ParseString(
		TEXT("<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6'>\n")
		TEXT("<node id='1' lat='0.0' lon='0.0'/>\n")
		TEXT("<node id='2' lat='0.0' lon='1.0'/>\n")
		TEXT("<way id='9'><nd ref='1'/><nd ref='2'/><tag k='highway' v='residential'/></way>\n")
		TEXT("<way id='10'><nd ref='1'/><nd ref='404'/><tag k='highway' v='residential'/></way>\n")
		TEXT("</osm>\n"));
	TestTrue(TEXT("Fixture parses"), Data.bSuccess);
	if (!Data.bSuccess) return false;

	// A converter standing in for the Cesium georeference: metres-ish per degree, and deliberately
	// Y-negating so it mirrors the East-South-Up frame the real one produces.
	auto FakeLonLatToWorld = [](double Lon, double Lat) { return FVector(Lon * 100.0, -Lat * 100.0, 0.0); };

	const FGRTOSMWay* Good = Data.Ways.FindByPredicate([](const FGRTOSMWay& W) { return W.Id == 9; });
	const FGRTOSMWay* Dangling = Data.Ways.FindByPredicate([](const FGRTOSMWay& W) { return W.Id == 10; });
	if (!Good || !Dangling) return false;

	TArray<FVector> Points;
	TestTrue(TEXT("A way whose nodes all exist resolves"),
		FGRTOSMIngest::ResolveWayWorldPoints(Data, *Good, FakeLonLatToWorld, Points));
	TestEqual(TEXT("...to one point per node ref"), Points.Num(), 2);
	TestTrue(TEXT("...through the supplied converter"), Points[1].Equals(FVector(100.0, 0.0, 0.0), 0.01));

	TestFalse(TEXT("A way referencing a node outside the extract is rejected whole, not built with a corner missing"),
		FGRTOSMIngest::ResolveWayWorldPoints(Data, *Dangling, FakeLonLatToWorld, Points));

	return true;
}

#endif // WITH_EDITOR
