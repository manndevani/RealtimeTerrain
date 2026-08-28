// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "IO/TerraDyneOSMParser.h"

#if WITH_EDITOR

namespace
{
	/**
	 * A miniature but structurally real .osm document: a closed 4-corner building with
	 * building:levels, a second building tagged only with height, a drivable residential road, a
	 * footway that must be rejected, and a building=no way that must also be rejected.
	 */
	const TCHAR* SampleOSM =
		TEXT("<?xml version='1.0' encoding='UTF-8'?>\n")
		TEXT("<osm version='0.6' generator='test'>\n")
		TEXT("  <bounds minlat='51.5000' minlon='-0.1300' maxlat='51.5020' maxlon='-0.1270'/>\n")
		TEXT("  <node id='1' lat='51.5000' lon='-0.1300'/>\n")
		TEXT("  <node id='2' lat='51.5000' lon='-0.1290'/>\n")
		TEXT("  <node id='3' lat='51.5010' lon='-0.1290'/>\n")
		TEXT("  <node id='4' lat='51.5010' lon='-0.1300'/>\n")
		TEXT("  <node id='5' lat='51.5015' lon='-0.1285'/>\n")
		TEXT("  <node id='6' lat='51.5018' lon='-0.1275'/>\n")
		TEXT("  <node id='7' lat='51.5020' lon='-0.1272'/>\n")
		TEXT("  <way id='100'>\n")
		TEXT("    <nd ref='1'/><nd ref='2'/><nd ref='3'/><nd ref='4'/><nd ref='1'/>\n")
		TEXT("    <tag k='building' v='yes'/>\n")
		TEXT("    <tag k='building:levels' v='5'/>\n")
		TEXT("  </way>\n")
		TEXT("  <way id='101'>\n")
		TEXT("    <nd ref='1'/><nd ref='2'/><nd ref='3'/><nd ref='1'/>\n")
		TEXT("    <tag k='building' v='residential'/>\n")
		TEXT("    <tag k='height' v='12 m'/>\n")
		TEXT("  </way>\n")
		TEXT("  <way id='102'>\n")
		TEXT("    <nd ref='5'/><nd ref='6'/><nd ref='7'/>\n")
		TEXT("    <tag k='highway' v='residential'/>\n")
		TEXT("    <tag k='lanes' v='2'/>\n")
		TEXT("  </way>\n")
		TEXT("  <way id='103'>\n")
		TEXT("    <nd ref='5'/><nd ref='6'/>\n")
		TEXT("    <tag k='highway' v='footway'/>\n")
		TEXT("  </way>\n")
		TEXT("  <way id='104'>\n")
		TEXT("    <nd ref='1'/><nd ref='2'/><nd ref='3'/><nd ref='1'/>\n")
		TEXT("    <tag k='building' v='no'/>\n")
		TEXT("  </way>\n")
		TEXT("</osm>\n");

	const FTerraDyneOSMWay* FindWay(const FTerraDyneOSMData& Data, uint64 Id)
	{
		return Data.Ways.FindByPredicate([Id](const FTerraDyneOSMWay& W) { return W.Id == Id; });
	}
}

// -----------------------------------------------------------------------------
// Document structure: nodes, ways, tags and bounds all survive the round trip.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneOSMParseTest, "TerraDyne.OSM.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneOSMParseTest::RunTest(const FString& Parameters)
{
	const FTerraDyneOSMData Data = FTerraDyneOSMParser::ParseString(SampleOSM);
	TestTrue(TEXT("Sample document parses"), Data.bSuccess);
	if (!Data.bSuccess)
	{
		AddError(Data.ErrorMessage);
		return false;
	}

	TestEqual(TEXT("All seven nodes read"), Data.Nodes.Num(), 7);
	TestEqual(TEXT("All five ways read"), Data.Ways.Num(), 5);

	// Ids are decimal. Reading them as hex (as at least one third-party OSM reader does) would put
	// way 100 under id 256 and silently lose every cross-reference.
	TestNotNull(TEXT("Way ids are parsed as decimal"), FindWay(Data, 100));

	const FTerraDyneOSMNode* Node1 = Data.Nodes.Find(1);
	TestNotNull(TEXT("Node 1 exists"), Node1);
	if (Node1)
	{
		TestTrue(TEXT("Node longitude survives full double precision"), FMath::IsNearlyEqual(Node1->Lon, -0.1300, 1e-9));
		TestTrue(TEXT("Node latitude survives full double precision"), FMath::IsNearlyEqual(Node1->Lat, 51.5000, 1e-9));
	}

	const FTerraDyneOSMWay* Building = FindWay(Data, 100);
	if (Building)
	{
		TestEqual(TEXT("Building way keeps all five node refs, including the repeated closing one"), Building->NodeIds.Num(), 5);
		TestTrue(TEXT("Building way is recognised as closed"), Building->IsClosed());
		const FString* Levels = Building->FindTag(TEXT("building:levels"));
		TestNotNull(TEXT("Tags with colons in the key are read intact"), Levels);
		if (Levels)
		{
			TestEqual(TEXT("Tag value round-trips"), *Levels, FString(TEXT("5")));
		}
	}

	TestTrue(TEXT("Explicit <bounds> is used"), Data.LonLatBounds.bIsValid);
	TestTrue(TEXT("Bounds min longitude matches the document"), FMath::IsNearlyEqual(Data.LonLatBounds.Min.X, -0.1300, 1e-9));
	TestTrue(TEXT("Bounds max latitude matches the document"), FMath::IsNearlyEqual(Data.LonLatBounds.Max.Y, 51.5020, 1e-9));

	return true;
}

// -----------------------------------------------------------------------------
// Classification: what actually becomes a building or a road.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneOSMClassifyTest, "TerraDyne.OSM.Classify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneOSMClassifyTest::RunTest(const FString& Parameters)
{
	const FTerraDyneOSMData Data = FTerraDyneOSMParser::ParseString(SampleOSM);
	if (!Data.bSuccess) return false;

	const FTerraDyneOSMWay* Tower = FindWay(Data, 100);
	const FTerraDyneOSMWay* ByHeight = FindWay(Data, 101);
	const FTerraDyneOSMWay* Road = FindWay(Data, 102);
	const FTerraDyneOSMWay* Footway = FindWay(Data, 103);
	const FTerraDyneOSMWay* NotABuilding = FindWay(Data, 104);
	if (!Tower || !ByHeight || !Road || !Footway || !NotABuilding) return false;

	TestTrue(TEXT("A closed building=yes way is a building"), FTerraDyneOSMParser::IsBuilding(*Tower));
	TestTrue(TEXT("building=residential is also a building"), FTerraDyneOSMParser::IsBuilding(*ByHeight));
	TestFalse(TEXT("building=no is explicitly not one"), FTerraDyneOSMParser::IsBuilding(*NotABuilding));
	TestFalse(TEXT("An open way is never a building, whatever it is tagged"), FTerraDyneOSMParser::IsBuilding(*Road));

	TestTrue(TEXT("highway=residential is drivable"), FTerraDyneOSMParser::IsDrivableHighway(*Road));
	TestFalse(TEXT("highway=footway is not drivable -- it must not become a carriageway"),
		FTerraDyneOSMParser::IsDrivableHighway(*Footway));
	TestFalse(TEXT("A building is not a road"), FTerraDyneOSMParser::IsDrivableHighway(*Tower));

	return true;
}

// -----------------------------------------------------------------------------
// Tag interpretation: storeys and widths, including the messy real-world spellings.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneOSMTagResolveTest, "TerraDyne.OSM.TagResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneOSMTagResolveTest::RunTest(const FString& Parameters)
{
	const FTerraDyneOSMData Data = FTerraDyneOSMParser::ParseString(SampleOSM);
	if (!Data.bSuccess) return false;

	const FTerraDyneOSMWay* Tower = FindWay(Data, 100);
	const FTerraDyneOSMWay* ByHeight = FindWay(Data, 101);
	const FTerraDyneOSMWay* Road = FindWay(Data, 102);
	if (!Tower || !ByHeight || !Road) return false;

	TestEqual(TEXT("building:levels is used directly"), FTerraDyneOSMParser::ResolveBuildingLevels(*Tower), 5);

	// "12 m" -- a unit suffix is normal in OSM and must not defeat the parse. 12 m / 3 m = 4 storeys.
	TestEqual(TEXT("height in metres falls back to 3m storeys, ignoring the unit suffix"),
		FTerraDyneOSMParser::ResolveBuildingLevels(*ByHeight), 4);

	// A way with neither tag takes the caller's default, and never returns zero storeys.
	FTerraDyneOSMWay Bare;
	Bare.NodeIds = { 1, 2, 3, 1 };
	TestEqual(TEXT("An untagged building takes the supplied default"),
		FTerraDyneOSMParser::ResolveBuildingLevels(Bare, 3), 3);
	TestEqual(TEXT("A zero default is still clamped to one storey"),
		FTerraDyneOSMParser::ResolveBuildingLevels(Bare, 0), 1);

	// lanes=2 -> 7 m.
	TestTrue(TEXT("lanes drives width when no explicit width is tagged"),
		FMath::IsNearlyEqual(FTerraDyneOSMParser::ResolveHighwayWidthMeters(*Road), 7.0f, 0.01f));

	// An explicit width tag wins over lanes.
	FTerraDyneOSMWay Wide;
	Wide.NodeIds = { 1, 2 };
	Wide.Tags.Add(FName(TEXT("highway")), TEXT("residential"));
	Wide.Tags.Add(FName(TEXT("lanes")), TEXT("2"));
	Wide.Tags.Add(FName(TEXT("width")), TEXT("11.5"));
	TestTrue(TEXT("An explicit width tag beats the lane estimate"),
		FMath::IsNearlyEqual(FTerraDyneOSMParser::ResolveHighwayWidthMeters(Wide), 11.5f, 0.01f));

	// Class fallback, and never zero -- a zero-width road would build degenerate geometry.
	FTerraDyneOSMWay Motorway;
	Motorway.NodeIds = { 1, 2 };
	Motorway.Tags.Add(FName(TEXT("highway")), TEXT("motorway"));
	TestTrue(TEXT("A class default applies when nothing else is tagged"),
		FTerraDyneOSMParser::ResolveHighwayWidthMeters(Motorway) > 10.0f);
	TestTrue(TEXT("Width is never zero"), FTerraDyneOSMParser::ResolveHighwayWidthMeters(Bare) > 0.0f);

	return true;
}

// -----------------------------------------------------------------------------
// Malformed input must fail with a reason rather than crashing or half-succeeding.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneOSMMalformedTest, "TerraDyne.OSM.MalformedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneOSMMalformedTest::RunTest(const FString& Parameters)
{
	const FTerraDyneOSMData Empty = FTerraDyneOSMParser::ParseString(FString());
	TestFalse(TEXT("An empty document fails"), Empty.bSuccess);
	TestTrue(TEXT("...with a reason"), !Empty.ErrorMessage.IsEmpty());

	const FTerraDyneOSMData Garbage = FTerraDyneOSMParser::ParseString(TEXT("<osm><node id='1' lat="));
	TestFalse(TEXT("Truncated XML fails"), Garbage.bSuccess);
	TestTrue(TEXT("...with a reason"), !Garbage.ErrorMessage.IsEmpty());

	// Valid XML, but nothing usable in it.
	const FTerraDyneOSMData NoNodes = FTerraDyneOSMParser::ParseString(
		TEXT("<?xml version='1.0'?><osm version='0.6'></osm>"));
	TestFalse(TEXT("A document with no nodes fails rather than reporting success with nothing"), NoNodes.bSuccess);

	// A way referencing a node that was never declared must not be dropped at parse time -- the
	// ingest layer decides what to do about dangling refs, and it needs to see them to say so.
	const FTerraDyneOSMData Dangling = FTerraDyneOSMParser::ParseString(
		TEXT("<?xml version='1.0'?><osm version='0.6'>")
		TEXT("<node id='1' lat='0.0' lon='0.0'/>")
		TEXT("<way id='9'><nd ref='1'/><nd ref='999'/><tag k='highway' v='residential'/></way>")
		TEXT("</osm>"));
	// This fixture is also deliberately written as a single line. FXmlFile is line-oriented and
	// rejects two elements that share a line outright, so without the parser's own normalisation
	// pass a minified .osm would fail with an unactionable "not valid XML".
	TestTrue(TEXT("A document with a dangling node ref still parses"), Dangling.bSuccess);
	if (Dangling.bSuccess && Dangling.Ways.Num() > 0)
	{
		TestEqual(TEXT("The dangling ref is preserved for the ingest layer to resolve"), Dangling.Ways[0].NodeIds.Num(), 2);
	}

	return true;
}

#endif // WITH_EDITOR
