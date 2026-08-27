// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadXodrExport.h"
#include "XmlFile.h"
#include "XmlNode.h"

namespace
{
	FXmlNode* CreateChild(FXmlNode* Parent, const TCHAR* Tag)
	{
		Parent->AppendChildNode(Tag);
		return Parent->GetChildrenNodes().Last();
	}

	FString FormatDouble(double Value)
	{
		// Fixed-point rather than RoadBuilder's own %e -- both are valid XML double lexical forms,
		// fixed-point is simply easier to eyeball/diff in a saved .xodr file. 8 decimals gives
		// sub-micron precision at the metre scale OpenDRIVE coordinates are exported in.
		return FString::Printf(TEXT("%.8f"), Value);
	}

	void AddAttribute(FXmlNode* Node, const TCHAR* Key, const FString& Value)
	{
		TArray<FXmlAttribute> Attributes = Node->GetAttributes();
		Attributes.Add(FXmlAttribute(Key, Value));
		Node->SetAttributes(Attributes);
	}

	void AddAttribute(FXmlNode* Node, const TCHAR* Key, int32 Value)
	{
		AddAttribute(Node, Key, FString::Printf(TEXT("%d"), Value));
	}

	void AddAttribute(FXmlNode* Node, const TCHAR* Key, double Value)
	{
		AddAttribute(Node, Key, FormatDouble(Value));
	}

	void AddABCD(FXmlNode* Node, const FVector4& ABCD)
	{
		AddAttribute(Node, TEXT("a"), static_cast<double>(ABCD.X));
		AddAttribute(Node, TEXT("b"), static_cast<double>(ABCD.Y));
		AddAttribute(Node, TEXT("c"), static_cast<double>(ABCD.Z));
		AddAttribute(Node, TEXT("d"), static_cast<double>(ABCD.W));
	}

	const TCHAR* LaneTypeToXodr(RoadXodrExport::ELaneExportType Type)
	{
		switch (Type)
		{
		case RoadXodrExport::ELaneExportType::Driving: return TEXT("driving");
		case RoadXodrExport::ELaneExportType::Shoulder: return TEXT("shoulder");
		case RoadXodrExport::ELaneExportType::Sidewalk: return TEXT("sidewalk");
		case RoadXodrExport::ELaneExportType::Median: return TEXT("median");
		default: return TEXT("none");
		}
	}

	void ExportLaneList(FXmlNode* SideNode, const TArray<RoadXodrExport::FLaneExportEntry>& Lanes, bool bNegativeIds)
	{
		// Index 0 is nearest the centreline in FRoadExportData's convention (mirroring
		// URoadStylePreset::LeftLanes/RightLanes, RoadStylePresets.h) -- which is exactly XODR's own
		// lane-id convention (id magnitude increases moving away from the centre), so index i maps
		// directly to id i+1 (left) / -(i+1) (right) with no reordering needed.
		for (int32 i = 0; i < Lanes.Num(); ++i)
		{
			const int32 LaneId = bNegativeIds ? -(i + 1) : (i + 1);
			FXmlNode* LaneNode = CreateChild(SideNode, TEXT("lane"));
			AddAttribute(LaneNode, TEXT("id"), LaneId);
			AddAttribute(LaneNode, TEXT("type"), LaneTypeToXodr(Lanes[i].Type));
			AddAttribute(LaneNode, TEXT("level"), FString(TEXT("false")));

			FXmlNode* WidthNode = CreateChild(LaneNode, TEXT("width"));
			AddAttribute(WidthNode, TEXT("sOffset"), 0.0);
			// Constant width for the lane's whole (single) section -- this project doesn't yet
			// author lane-width keys that vary along a road's length, so only the "a" coefficient
			// (the constant term) is nonzero, matching RoadBuilder's own ExportXodr for a lane with
			// no further width key ahead of it.
			AddABCD(WidthNode, FVector4(Lanes[i].WidthCm * 0.01, 0.0, 0.0, 0.0));
		}
	}

	// FXmlFile::WriteNodeHierarchy (XmlFile.cpp) does exactly this, but is a private member -- not
	// reachable from outside the XmlParser module. Reimplemented here (same algorithm: escape
	// attribute values, self-close an empty/no-content node, otherwise recurse into children or emit
	// text content) so ExportOpenDrive can serialise to an in-memory string without touching disk.
	void WriteXmlNode(const FXmlNode& Node, const FString& Indent, FString& Output)
	{
		Output += Indent + FString::Printf(TEXT("<%s"), *Node.GetTag());
		for (const FXmlAttribute& Attribute : Node.GetAttributes())
		{
			FString EscapedValue = Attribute.GetValue();
			EscapedValue.ReplaceInline(TEXT("&"), TEXT("&amp;"), ESearchCase::CaseSensitive);
			EscapedValue.ReplaceInline(TEXT("\""), TEXT("&quot;"), ESearchCase::CaseSensitive);
			EscapedValue.ReplaceInline(TEXT("'"), TEXT("&apos;"), ESearchCase::CaseSensitive);
			EscapedValue.ReplaceInline(TEXT("<"), TEXT("&lt;"), ESearchCase::CaseSensitive);
			EscapedValue.ReplaceInline(TEXT(">"), TEXT("&gt;"), ESearchCase::CaseSensitive);
			Output += FString::Printf(TEXT(" %s=\"%s\""), *Attribute.GetTag(), *EscapedValue);
		}

		const TArray<FXmlNode*>& Children = Node.GetChildrenNodes();
		if (Children.Num() == 0)
		{
			const FString& Content = Node.GetContent();
			if (Content.Len() == 0)
			{
				Output += TEXT(" />") LINE_TERMINATOR;
			}
			else
			{
				Output += TEXT(">") + Content + FString::Printf(TEXT("</%s>"), *Node.GetTag()) + LINE_TERMINATOR;
			}
		}
		else
		{
			Output += TEXT(">") LINE_TERMINATOR;
			for (const FXmlNode* Child : Children)
			{
				WriteXmlNode(*Child, Indent + TEXT("\t"), Output);
			}
			Output += Indent + FString::Printf(TEXT("</%s>"), *Node.GetTag()) + LINE_TERMINATOR;
		}
	}

	void ExportRoad(FXmlNode* RootNode, const RoadXodrExport::FRoadExportData& Road)
	{
		if (Road.CentrelineXY.Num() < 2 || Road.Distances.Num() != Road.CentrelineXY.Num())
		{
			return;
		}

		const double TotalLengthM = Road.Distances.Last() * 0.01;

		FXmlNode* RoadNode = CreateChild(RootNode, TEXT("road"));
		AddAttribute(RoadNode, TEXT("name"), Road.Name);
		AddAttribute(RoadNode, TEXT("id"), Road.Id);
		AddAttribute(RoadNode, TEXT("junction"), -1);
		AddAttribute(RoadNode, TEXT("length"), TotalLengthM);

		FXmlNode* PlanViewNode = CreateChild(RoadNode, TEXT("planView"));
		for (int32 i = 0; i + 1 < Road.CentrelineXY.Num(); ++i)
		{
			const FVector2D& P0 = Road.CentrelineXY[i];
			const FVector2D& P1 = Road.CentrelineXY[i + 1];
			const double SegLengthCm = Road.Distances[i + 1] - Road.Distances[i];
			if (SegLengthCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// Y (and therefore heading) is negated going from Unreal's world XY into OpenDRIVE's
			// coordinate convention -- the same flip RoadBuilder's own FRoadSegment::ExportXodr
			// applies (RoadActor.cpp), ported here rather than re-derived since it's a fixed,
			// well-established convention, not something a geometry test would independently catch.
			const double Heading = -FMath::Atan2(P1.Y - P0.Y, P1.X - P0.X);

			FXmlNode* GeometryNode = CreateChild(PlanViewNode, TEXT("geometry"));
			AddAttribute(GeometryNode, TEXT("s"), Road.Distances[i] * 0.01);
			AddAttribute(GeometryNode, TEXT("x"), P0.X * 0.01);
			AddAttribute(GeometryNode, TEXT("y"), -P0.Y * 0.01);
			AddAttribute(GeometryNode, TEXT("hdg"), Heading);
			AddAttribute(GeometryNode, TEXT("length"), SegLengthCm * 0.01);
			CreateChild(GeometryNode, TEXT("line"));
		}

		// Always emitted, even with no records -- an empty elevationProfile is how OpenDRIVE
		// expresses "follows the plan view's flat Z=0 plane", not an error, and RoadBuilder's own
		// exporter always creates this node too regardless of content.
		FXmlNode* ElevationProfileNode = CreateChild(RoadNode, TEXT("elevationProfile"));
		if (Road.HeightKeys.Num() >= 2)
		{
			TArray<FRoadHeightKey> Keys = Road.HeightKeys;
			Keys.Sort();
			for (int32 i = 0; i + 1 < Keys.Num(); ++i)
			{
				const FRoadHeightKey& A = Keys[i];
				const FRoadHeightKey& B = Keys[i + 1];
				const double SegLengthM = (B.Dist - A.Dist) * 0.01;
				if (SegLengthM <= UE_DOUBLE_KINDA_SMALL_NUMBER)
				{
					continue;
				}
				const FVector4 ABCD = RoadXodrExport::CalcElevationPolynomial(
					A.Height * 0.01, A.Slope, B.Height * 0.01, B.Slope, SegLengthM);
				FXmlNode* ElevationNode = CreateChild(ElevationProfileNode, TEXT("elevation"));
				AddAttribute(ElevationNode, TEXT("s"), A.Dist * 0.01);
				AddABCD(ElevationNode, ABCD);
			}
		}

		FXmlNode* LanesNode = CreateChild(RoadNode, TEXT("lanes"));
		FXmlNode* SectionNode = CreateChild(LanesNode, TEXT("laneSection"));
		AddAttribute(SectionNode, TEXT("s"), 0.0);

		FXmlNode* LeftNode = CreateChild(SectionNode, TEXT("left"));
		FXmlNode* CenterNode = CreateChild(SectionNode, TEXT("center"));
		FXmlNode* RightNode = CreateChild(SectionNode, TEXT("right"));

		ExportLaneList(LeftNode, Road.LeftLanes, false);

		FXmlNode* CenterLaneNode = CreateChild(CenterNode, TEXT("lane"));
		AddAttribute(CenterLaneNode, TEXT("id"), 0);
		AddAttribute(CenterLaneNode, TEXT("type"), FString(TEXT("none")));
		AddAttribute(CenterLaneNode, TEXT("level"), FString(TEXT("false")));

		ExportLaneList(RightNode, Road.RightLanes, true);
	}
}

FVector4 RoadXodrExport::CalcElevationPolynomial(double P0, double M0, double P1, double M1, double SegmentLength)
{
	if (SegmentLength <= 0.0)
	{
		return FVector4(P0, 0.0, 0.0, 0.0);
	}

	// Direct cubic Hermite fit in the real (non-normalised) ds domain: f(0)=P0, f'(0)=M0, f(D)=P1,
	// f'(D)=M1. Unlike a t-in-[0,1] Hermite basis, M0/M1 here are the TRUE derivatives (dValue/dds),
	// not pre-scaled by D -- matching RoadElevationProfile::EvaluateHeight's own Slope convention,
	// so a road's authored HeightKeys plug in without any extra scaling at the call site.
	const double D = SegmentLength;
	const double D2 = D * D;
	const double D3 = D2 * D;
	const double A = P0;
	const double B = M0;
	const double C = (3.0 * (P1 - P0) - D * (2.0 * M0 + M1)) / D2;
	const double Dc = (2.0 * (P0 - P1) + D * (M0 + M1)) / D3;
	return FVector4(A, B, C, Dc);
}

FString RoadXodrExport::ExportOpenDrive(const TArray<FRoadExportData>& Roads)
{
	FXmlFile XmlFile(TEXT("<OpenDRIVE/>"), EConstructMethod::ConstructFromBuffer);
	FXmlNode* RootNode = XmlFile.GetRootNode();
	if (!RootNode)
	{
		return FString();
	}

	// Bounding box (metres, OpenDRIVE's Y-negated convention) across every exported road's
	// centreline -- required by the <header> element's north/south/east/west attributes.
	FVector2D BoundsMin(0.0, 0.0);
	FVector2D BoundsMax(0.0, 0.0);
	bool bHasAnyPoint = false;
	for (const FRoadExportData& Road : Roads)
	{
		for (const FVector2D& Point : Road.CentrelineXY)
		{
			const FVector2D Exported(Point.X * 0.01, -Point.Y * 0.01);
			if (!bHasAnyPoint)
			{
				BoundsMin = BoundsMax = Exported;
				bHasAnyPoint = true;
			}
			else
			{
				BoundsMin = FVector2D(FMath::Min(BoundsMin.X, Exported.X), FMath::Min(BoundsMin.Y, Exported.Y));
				BoundsMax = FVector2D(FMath::Max(BoundsMax.X, Exported.X), FMath::Max(BoundsMax.Y, Exported.Y));
			}
		}
	}

	// RoadBuilder's own ExportXodr never writes a <header> element at all -- a real gap against the
	// ASAM OpenDRIVE spec (some strict consumers require it), not a convention worth reproducing.
	FXmlNode* HeaderNode = CreateChild(RootNode, TEXT("header"));
	AddAttribute(HeaderNode, TEXT("revMajor"), 1);
	AddAttribute(HeaderNode, TEXT("revMinor"), 4);
	AddAttribute(HeaderNode, TEXT("name"), FString(TEXT("RealtimeTerrain Export")));
	AddAttribute(HeaderNode, TEXT("version"), FString(TEXT("1.00")));
	AddAttribute(HeaderNode, TEXT("north"), BoundsMax.Y);
	AddAttribute(HeaderNode, TEXT("south"), BoundsMin.Y);
	AddAttribute(HeaderNode, TEXT("east"), BoundsMax.X);
	AddAttribute(HeaderNode, TEXT("west"), BoundsMin.X);

	for (const FRoadExportData& Road : Roads)
	{
		ExportRoad(RootNode, Road);
	}

	FString Output = TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") LINE_TERMINATOR;
	WriteXmlNode(*RootNode, FString(), Output);
	return Output;
}
