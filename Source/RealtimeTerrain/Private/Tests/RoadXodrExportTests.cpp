// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadXodrExport.h"
#include "XmlFile.h"
#include "XmlNode.h"

// Verifies CalcElevationPolynomial's cubic Hermite-to-power-basis conversion round-trips its own
// inputs exactly: evaluating the returned polynomial (and its derivative) at ds=0 and ds=SegmentLength
// must reproduce P0/M0/P1/M1. This is the correctness property RoadBuilder's own equivalent call
// site (FHeightSegment::ExportXodr) does NOT satisfy -- it passes its tangent to CalcABCD without
// multiplying by the segment length first, which cross-checking against its own actively-used
// FHeightSegment::Get (a FMath::CubicInterp call that DOES scale by length) shows is inconsistent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadXodrElevationPolynomialTest, "TerraDyne.RoadXodrExport.ElevationPolynomialRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadXodrElevationPolynomialTest::RunTest(const FString& Parameters)
{
	const double P0 = 100.0;
	const double M0 = 0.2;
	const double P1 = 150.0;
	const double M1 = -0.1;
	const double D = 50.0;

	const FVector4 ABCD = RoadXodrExport::CalcElevationPolynomial(P0, M0, P1, M1, D);
	auto Evaluate = [&](double DS) { return ABCD.X + ABCD.Y * DS + ABCD.Z * DS * DS + ABCD.W * DS * DS * DS; };
	auto EvaluateDerivative = [&](double DS) { return ABCD.Y + 2.0 * ABCD.Z * DS + 3.0 * ABCD.W * DS * DS; };

	AddInfo(FString::Printf(TEXT("ABCD=(%.6f,%.6f,%.6f,%.6f)"), ABCD.X, ABCD.Y, ABCD.Z, ABCD.W));
	TestEqual(TEXT("f(0) reproduces P0"), Evaluate(0.0), P0, 1.0e-6);
	TestEqual(TEXT("f(D) reproduces P1"), Evaluate(D), P1, 1.0e-6);
	TestEqual(TEXT("f'(0) reproduces M0"), EvaluateDerivative(0.0), M0, 1.0e-6);
	TestEqual(TEXT("f'(D) reproduces M1"), EvaluateDerivative(D), M1, 1.0e-6);
	return true;
}

// Verifies ExportOpenDrive produces a real, structurally correct .xodr document by parsing its own
// output back (FXmlFile can load from an in-memory buffer, so this needs no disk I/O) and checking
// specific numeric attributes against the known input -- not just "produced a non-empty string".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadXodrExportStructureTest, "TerraDyne.RoadXodrExport.ExportedDocumentStructureAndValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadXodrExportStructureTest::RunTest(const FString& Parameters)
{
	RoadXodrExport::FRoadExportData Road;
	Road.Name = TEXT("TestRoad");
	Road.Id = 5;
	// A 10m run along +X, then a 20m run along +Y -- an L-shaped two-segment centreline.
	Road.CentrelineXY = { FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0), FVector2D(1000.0, 2000.0) };
	Road.Distances = { 0.0, 1000.0, 3000.0 };
	Road.HeightKeys = { { 0.0, 0.0, 0.0 }, { 3000.0, 500.0, 0.0 } };
	Road.RightLanes = {
		{ RoadXodrExport::ELaneExportType::Driving, 350.0 },
		{ RoadXodrExport::ELaneExportType::Sidewalk, 200.0 },
	};

	const FString Xml = RoadXodrExport::ExportOpenDrive({ Road });
	TestTrue(TEXT("Export produced non-empty XML"), Xml.Len() > 0);
	AddInfo(Xml.Left(400));

	FXmlFile Parsed(Xml, EConstructMethod::ConstructFromBuffer);
	TestTrue(TEXT("Exported XML re-parses without error"), Parsed.IsValid());
	if (!Parsed.IsValid()) return false;

	const FXmlNode* RootNode = Parsed.GetRootNode();
	TestNotNull(TEXT("Root node exists"), RootNode);
	if (!RootNode) return false;
	TestEqual(TEXT("Root tag is OpenDRIVE"), RootNode->GetTag(), FString(TEXT("OpenDRIVE")));

	const FXmlNode* HeaderNode = RootNode->FindChildNode(TEXT("header"));
	TestNotNull(TEXT("header node exists"), HeaderNode);
	if (HeaderNode)
	{
		TestEqual(TEXT("header revMajor"), HeaderNode->GetAttribute(TEXT("revMajor")), FString(TEXT("1")));
	}

	const FXmlNode* RoadNode = RootNode->FindChildNode(TEXT("road"));
	TestNotNull(TEXT("road node exists"), RoadNode);
	if (!RoadNode) return false;
	TestEqual(TEXT("road id"), RoadNode->GetAttribute(TEXT("id")), FString(TEXT("5")));
	TestEqual(TEXT("road name"), RoadNode->GetAttribute(TEXT("name")), FString(TEXT("TestRoad")));
	TestEqual(TEXT("road length is 30m (10m + 20m)"), FCString::Atod(*RoadNode->GetAttribute(TEXT("length"))), 30.0, 0.001);

	const FXmlNode* PlanViewNode = RoadNode->FindChildNode(TEXT("planView"));
	TestNotNull(TEXT("planView node exists"), PlanViewNode);
	if (PlanViewNode)
	{
		const TArray<FXmlNode*>& Geometries = PlanViewNode->GetChildrenNodes();
		TestEqual(TEXT("planView has one geometry per centreline segment (2)"), Geometries.Num(), 2);
		if (Geometries.Num() == 2)
		{
			AddInfo(FString::Printf(TEXT("geom0 x=%s y=%s hdg=%s length=%s"),
				*Geometries[0]->GetAttribute(TEXT("x")), *Geometries[0]->GetAttribute(TEXT("y")),
				*Geometries[0]->GetAttribute(TEXT("hdg")), *Geometries[0]->GetAttribute(TEXT("length"))));
			TestEqual(TEXT("geom0 x=0"), FCString::Atod(*Geometries[0]->GetAttribute(TEXT("x"))), 0.0, 0.001);
			TestEqual(TEXT("geom0 heading=0 (heads along +X)"), FCString::Atod(*Geometries[0]->GetAttribute(TEXT("hdg"))), 0.0, 0.001);
			TestEqual(TEXT("geom0 length=10m"), FCString::Atod(*Geometries[0]->GetAttribute(TEXT("length"))), 10.0, 0.001);

			AddInfo(FString::Printf(TEXT("geom1 x=%s y=%s hdg=%s length=%s"),
				*Geometries[1]->GetAttribute(TEXT("x")), *Geometries[1]->GetAttribute(TEXT("y")),
				*Geometries[1]->GetAttribute(TEXT("hdg")), *Geometries[1]->GetAttribute(TEXT("length"))));
			TestEqual(TEXT("geom1 x=10m"), FCString::Atod(*Geometries[1]->GetAttribute(TEXT("x"))), 10.0, 0.001);
			// The second run heads along +Y (world), which is heading -PI/2 under the Y-negated
			// OpenDRIVE convention this exporter applies (matching RoadBuilder's own sign flip).
			TestEqual(TEXT("geom1 heading=-PI/2 (heads along +Y, Y-negated convention)"), FCString::Atod(*Geometries[1]->GetAttribute(TEXT("hdg"))), -PI / 2.0, 0.001);
			TestEqual(TEXT("geom1 length=20m"), FCString::Atod(*Geometries[1]->GetAttribute(TEXT("length"))), 20.0, 0.001);
		}
	}

	const FXmlNode* ElevationProfileNode = RoadNode->FindChildNode(TEXT("elevationProfile"));
	TestNotNull(TEXT("elevationProfile node exists"), ElevationProfileNode);
	if (ElevationProfileNode)
	{
		const TArray<FXmlNode*>& Elevations = ElevationProfileNode->GetChildrenNodes();
		TestEqual(TEXT("One elevation record for two height keys"), Elevations.Num(), 1);
		if (Elevations.Num() == 1)
		{
			const double A = FCString::Atod(*Elevations[0]->GetAttribute(TEXT("a")));
			const double B = FCString::Atod(*Elevations[0]->GetAttribute(TEXT("b")));
			const double C = FCString::Atod(*Elevations[0]->GetAttribute(TEXT("c")));
			const double Dc = FCString::Atod(*Elevations[0]->GetAttribute(TEXT("d")));
			AddInfo(FString::Printf(TEXT("elevation a=%.6f b=%.6f c=%.6f d=%.6f"), A, B, C, Dc));
			TestEqual(TEXT("elevation starts at height 0 (a=P0)"), A, 0.0, 0.001);
			const double EndHeight = A + B * 30.0 + C * 30.0 * 30.0 + Dc * 30.0 * 30.0 * 30.0;
			TestEqual(TEXT("elevation polynomial reaches 5m (500cm) at s=30m"), EndHeight, 5.0, 0.001);
		}
	}

	const FXmlNode* LanesNode = RoadNode->FindChildNode(TEXT("lanes"));
	TestNotNull(TEXT("lanes node exists"), LanesNode);
	if (LanesNode)
	{
		const FXmlNode* SectionNode = LanesNode->FindChildNode(TEXT("laneSection"));
		TestNotNull(TEXT("laneSection node exists"), SectionNode);
		if (SectionNode)
		{
			const FXmlNode* LeftNode = SectionNode->FindChildNode(TEXT("left"));
			const FXmlNode* CenterNode = SectionNode->FindChildNode(TEXT("center"));
			const FXmlNode* RightNode = SectionNode->FindChildNode(TEXT("right"));
			TestNotNull(TEXT("left node exists"), LeftNode);
			TestNotNull(TEXT("center node exists"), CenterNode);
			TestNotNull(TEXT("right node exists"), RightNode);

			if (LeftNode)
			{
				TestEqual(TEXT("No left lanes were authored"), LeftNode->GetChildrenNodes().Num(), 0);
			}
			if (CenterNode)
			{
				const TArray<FXmlNode*>& CenterLanes = CenterNode->GetChildrenNodes();
				TestEqual(TEXT("Exactly one center lane (id 0)"), CenterLanes.Num(), 1);
				if (CenterLanes.Num() == 1)
				{
					TestEqual(TEXT("Center lane id is 0"), CenterLanes[0]->GetAttribute(TEXT("id")), FString(TEXT("0")));
				}
			}
			if (RightNode)
			{
				const TArray<FXmlNode*>& RightLanesXml = RightNode->GetChildrenNodes();
				TestEqual(TEXT("Two right lanes authored"), RightLanesXml.Num(), 2);
				if (RightLanesXml.Num() == 2)
				{
					TestEqual(TEXT("First right lane id=-1"), RightLanesXml[0]->GetAttribute(TEXT("id")), FString(TEXT("-1")));
					TestEqual(TEXT("First right lane type=driving"), RightLanesXml[0]->GetAttribute(TEXT("type")), FString(TEXT("driving")));
					const FXmlNode* WidthNode = RightLanesXml[0]->FindChildNode(TEXT("width"));
					TestNotNull(TEXT("First right lane has a width node"), WidthNode);
					if (WidthNode)
					{
						TestEqual(TEXT("First right lane width is 3.5m (350cm)"), FCString::Atod(*WidthNode->GetAttribute(TEXT("a"))), 3.5, 0.001);
					}

					TestEqual(TEXT("Second right lane id=-2"), RightLanesXml[1]->GetAttribute(TEXT("id")), FString(TEXT("-2")));
					TestEqual(TEXT("Second right lane type=sidewalk"), RightLanesXml[1]->GetAttribute(TEXT("type")), FString(TEXT("sidewalk")));
				}
			}
		}
	}

	return true;
}
