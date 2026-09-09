// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/GRTOSMParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "XmlFile.h"

namespace
{
	/**
	 * highway values that carry vehicles. Everything absent from this list -- footway, path,
	 * cycleway, steps, pedestrian, bridleway, corridor, proposed, construction -- is skipped
	 * rather than built as a carriageway.
	 */
	const TSet<FString>& DrivableHighwayValues()
	{
		static const TSet<FString> Values = {
			TEXT("motorway"),      TEXT("motorway_link"),
			TEXT("trunk"),         TEXT("trunk_link"),
			TEXT("primary"),       TEXT("primary_link"),
			TEXT("secondary"),     TEXT("secondary_link"),
			TEXT("tertiary"),      TEXT("tertiary_link"),
			TEXT("unclassified"),  TEXT("residential"),
			TEXT("living_street"), TEXT("service")
		};
		return Values;
	}

	/** Fallback carriageway widths in metres, by class, when neither width nor lanes is tagged. */
	float DefaultWidthForHighwayClass(const FString& Value)
	{
		if (Value == TEXT("motorway") || Value == TEXT("trunk"))   return 14.0f;
		if (Value == TEXT("primary"))                              return 10.0f;
		if (Value == TEXT("secondary"))                            return 9.0f;
		if (Value == TEXT("tertiary"))                             return 8.0f;
		if (Value == TEXT("service"))                              return 4.0f;
		if (Value == TEXT("living_street"))                        return 5.0f;
		if (Value.EndsWith(TEXT("_link")))                         return 6.0f;
		return 6.5f; // residential / unclassified
	}

	/**
	 * OSM numeric tags are free text and routinely carry units or ranges -- "12 m", "3;4", "~10".
	 * Take the leading number and ignore the rest rather than rejecting the tag outright.
	 */
	bool ParseLeadingDouble(const FString& Raw, double& OutValue)
	{
		const FString Trimmed = Raw.TrimStartAndEnd();
		int32 End = 0;
		bool bSeenDigit = false;
		bool bSeenDot = false;
		while (End < Trimmed.Len())
		{
			const TCHAR C = Trimmed[End];
			if (FChar::IsDigit(C)) { bSeenDigit = true; }
			else if (C == TEXT('.') && !bSeenDot) { bSeenDot = true; }
			else if ((C == TEXT('-') || C == TEXT('+')) && End == 0) { /* sign */ }
			else { break; }
			++End;
		}

		if (!bSeenDigit)
		{
			return false;
		}
		OutValue = FCString::Atod(*Trimmed.Left(End));
		return true;
	}
}

bool FGRTOSMParser::IsSupportedExtension(const FString& Extension)
{
	const FString Lower = Extension.ToLower();
	return Lower == TEXT("osm") || Lower == TEXT("xml");
}

FGRTOSMData FGRTOSMParser::ParseFile(const FString& FilePath)
{
	FGRTOSMData Result;

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Could not read OSM file '%s'."), *FilePath);
		return Result;
	}

	return ParseString(Content);
}

FGRTOSMData FGRTOSMParser::ParseString(const FString& XmlContent)
{
	FGRTOSMData Result;

	if (XmlContent.IsEmpty())
	{
		Result.ErrorMessage = TEXT("The OSM document is empty.");
		return Result;
	}

	// FXmlFile is line-oriented: it cannot parse two elements sharing a line, and rejects the whole
	// document with a bare "Failed to parse the loaded document" when they do. Exports from
	// openstreetmap.org put one element per line so they load as-is, but anything minified would
	// fail for a reason the user could never act on. Splitting element boundaries onto their own
	// lines first is safe here specifically because OSM XML carries all of its data in attributes
	// and never in element text -- and a literal "><" cannot occur inside a properly escaped
	// attribute value, since those must be written &gt; and &lt;.
	FString Normalized = XmlContent;
	Normalized.ReplaceInline(TEXT("><"), TEXT(">\n<"), ESearchCase::CaseSensitive);

	FXmlFile XmlFile(Normalized, EConstructMethod::ConstructFromBuffer);
	if (!XmlFile.IsValid())
	{
		Result.ErrorMessage = FString::Printf(TEXT("The OSM document is not valid XML: %s"), *XmlFile.GetLastError());
		return Result;
	}

	const FXmlNode* Root = XmlFile.GetRootNode();
	if (!Root)
	{
		Result.ErrorMessage = TEXT("The OSM document has no root element.");
		return Result;
	}

	bool bHasExplicitBounds = false;

	for (const FXmlNode* Child : Root->GetChildrenNodes())
	{
		if (!Child)
		{
			continue;
		}

		const FString Tag = Child->GetTag();

		if (Tag == TEXT("node"))
		{
			// OSM ids are decimal, not hex.
			const uint64 Id = FCString::Strtoui64(*Child->GetAttribute(TEXT("id")), nullptr, 10);
			if (Id == 0)
			{
				continue;
			}

			FGRTOSMNode Node;
			Node.Lon = FCString::Atod(*Child->GetAttribute(TEXT("lon")));
			Node.Lat = FCString::Atod(*Child->GetAttribute(TEXT("lat")));
			Result.Nodes.Add(Id, Node);
		}
		else if (Tag == TEXT("way"))
		{
			FGRTOSMWay Way;
			Way.Id = FCString::Strtoui64(*Child->GetAttribute(TEXT("id")), nullptr, 10);

			for (const FXmlNode* WayChild : Child->GetChildrenNodes())
			{
				if (!WayChild)
				{
					continue;
				}
				const FString WayChildTag = WayChild->GetTag();
				if (WayChildTag == TEXT("nd"))
				{
					const uint64 Ref = FCString::Strtoui64(*WayChild->GetAttribute(TEXT("ref")), nullptr, 10);
					if (Ref != 0)
					{
						Way.NodeIds.Add(Ref);
					}
				}
				else if (WayChildTag == TEXT("tag"))
				{
					const FString Key = WayChild->GetAttribute(TEXT("k"));
					if (!Key.IsEmpty())
					{
						Way.Tags.Add(FName(*Key), WayChild->GetAttribute(TEXT("v")));
					}
				}
			}

			if (Way.NodeIds.Num() > 0)
			{
				Result.Ways.Add(MoveTemp(Way));
			}
		}
		else if (Tag == TEXT("bounds"))
		{
			const double MinLon = FCString::Atod(*Child->GetAttribute(TEXT("minlon")));
			const double MinLat = FCString::Atod(*Child->GetAttribute(TEXT("minlat")));
			const double MaxLon = FCString::Atod(*Child->GetAttribute(TEXT("maxlon")));
			const double MaxLat = FCString::Atod(*Child->GetAttribute(TEXT("maxlat")));
			Result.LonLatBounds = FBox2D(FVector2D(MinLon, MinLat), FVector2D(MaxLon, MaxLat));
			bHasExplicitBounds = true;
		}
		// <relation> is deliberately ignored for now: multipolygon buildings with holes are the
		// main thing it buys, and neither the building footprint spline nor the road system can
		// express a hole today.
	}

	if (!bHasExplicitBounds)
	{
		for (const TPair<uint64, FGRTOSMNode>& Pair : Result.Nodes)
		{
			Result.LonLatBounds += FVector2D(Pair.Value.Lon, Pair.Value.Lat);
		}
	}

	if (Result.Nodes.Num() == 0)
	{
		Result.ErrorMessage = TEXT("The OSM document contained no nodes.");
		return Result;
	}

	// Build topological node-to-way index (like fullike / OpenDRIVE)
	for (int32 WayIdx = 0; WayIdx < Result.Ways.Num(); ++WayIdx)
	{
		const FGRTOSMWay& Way = Result.Ways[WayIdx];
		for (uint64 NodeId : Way.NodeIds)
		{
			Result.NodeToWayIndices.FindOrAdd(NodeId).Add(WayIdx);
		}
	}

	for (const auto& Pair : Result.NodeToWayIndices)
	{
		int32 DrivableCount = 0;
		for (int32 WayIdx : Pair.Value)
		{
			if (Result.Ways.IsValidIndex(WayIdx) && IsDrivableHighway(Result.Ways[WayIdx]))
			{
				++DrivableCount;
			}
		}
		if (DrivableCount > 1)
		{
			Result.JunctionNodeIds.Add(Pair.Key);
		}
	}

	Result.bSuccess = true;
	return Result;
}

bool FGRTOSMParser::IsBuilding(const FGRTOSMWay& Way)
{
	if (!Way.IsClosed())
	{
		return false;
	}

	// building=no is an explicit statement that this is not one.
	if (const FString* Building = Way.FindTag(TEXT("building")))
	{
		return *Building != TEXT("no");
	}
	return Way.HasTag(TEXT("building:part"));
}

bool FGRTOSMParser::IsDrivableHighway(const FGRTOSMWay& Way)
{
	const FString* Highway = Way.FindTag(TEXT("highway"));
	if (!Highway)
	{
		return false;
	}

	// A road needs at least two distinct points to have a direction.
	if (Way.NodeIds.Num() < 2)
	{
		return false;
	}

	return DrivableHighwayValues().Contains(Highway->ToLower());
}

int32 FGRTOSMParser::ResolveBuildingLevels(const FGRTOSMWay& Way, int32 DefaultLevels)
{
	double Value = 0.0;

	if (const FString* Levels = Way.FindTag(TEXT("building:levels")))
	{
		if (ParseLeadingDouble(*Levels, Value) && Value >= 1.0)
		{
			return FMath::Max(1, FMath::RoundToInt(Value));
		}
	}

	// height is in metres. Three metres per storey is the usual OSM convention.
	if (const FString* Height = Way.FindTag(TEXT("height")))
	{
		if (ParseLeadingDouble(*Height, Value) && Value > 0.0)
		{
			return FMath::Max(1, FMath::RoundToInt(Value / 3.0));
		}
	}

	return FMath::Max(1, DefaultLevels);
}

float FGRTOSMParser::ResolveHighwayWidthMeters(const FGRTOSMWay& Way)
{
	double Value = 0.0;

	if (const FString* Width = Way.FindTag(TEXT("width")))
	{
		if (ParseLeadingDouble(*Width, Value) && Value > 0.0)
		{
			return static_cast<float>(Value);
		}
	}

	if (const FString* Lanes = Way.FindTag(TEXT("lanes")))
	{
		if (ParseLeadingDouble(*Lanes, Value) && Value >= 1.0)
		{
			return static_cast<float>(Value * 3.5);
		}
	}

	const FString* Highway = Way.FindTag(TEXT("highway"));
	return DefaultWidthForHighwayClass(Highway ? Highway->ToLower() : FString());
}
