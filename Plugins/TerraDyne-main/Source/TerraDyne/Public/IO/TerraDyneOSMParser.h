// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** One OSM node: a bare lon/lat point. Ways reference these by id. */
struct TERRADYNE_API FTerraDyneOSMNode
{
	double Lon = 0.0;
	double Lat = 0.0;
};

/** One OSM way: an ordered list of node ids plus its tags. */
struct TERRADYNE_API FTerraDyneOSMWay
{
	uint64 Id = 0;
	TArray<uint64> NodeIds;
	TMap<FName, FString> Tags;

	/** A way is an area when its first and last node are the same one. */
	bool IsClosed() const
	{
		return NodeIds.Num() >= 4 && NodeIds[0] == NodeIds.Last();
	}

	const FString* FindTag(const TCHAR* Key) const
	{
		return Tags.Find(FName(Key));
	}

	bool HasTag(const TCHAR* Key) const
	{
		return Tags.Contains(FName(Key));
	}
};

/** Everything one .osm document yielded. */
struct TERRADYNE_API FTerraDyneOSMData
{
	bool bSuccess = false;
	FString ErrorMessage;

	TMap<uint64, FTerraDyneOSMNode> Nodes;
	TArray<FTerraDyneOSMWay> Ways;

	/** Topological node-to-way index: which ways reference each node id. */
	TMap<uint64, TArray<int32>> NodeToWayIndices;

	/** Node IDs referenced by multiple drivable ways (topological intersections). */
	TSet<uint64> JunctionNodeIds;

	/** From the document's <bounds> element when present, otherwise the extent of the nodes read. */
	FBox2D LonLatBounds = FBox2D(ForceInit);
};

/**
 * Reader for OpenStreetMap XML (.osm) documents.
 *
 * Deliberately pure: no world, no UObject, no georeferencing. It turns a document into nodes, ways
 * and tags, and answers questions about those tags -- nothing more. Everything that needs a world
 * (lon/lat to Unreal, spawning buildings and roads) lives in the ingest layer above it, so the
 * fiddly part -- what counts as a building, how many storeys a tag set implies, how wide a road
 * class is -- can be tested exhaustively with no map open.
 *
 * XML rather than the binary .osm.pbf format: FXmlFile is a runtime engine module and needs no
 * third-party dependency, and the OSM export people actually download by hand is XML.
 */
class TERRADYNE_API FTerraDyneOSMParser
{
public:
	/** Parses a .osm document from disk. Check bSuccess/ErrorMessage on the result. */
	static FTerraDyneOSMData ParseFile(const FString& FilePath);

	/** Parses a .osm document already in memory -- the form the tests and any future download path use. */
	static FTerraDyneOSMData ParseString(const FString& XmlContent);

	/** True when this way describes a building footprint: tagged as one, and closed. */
	static bool IsBuilding(const FTerraDyneOSMWay& Way);

	/**
	 * True when this way is a road vehicles drive on. Footpaths, cycleways, steps and pedestrian
	 * areas are excluded -- the road system builds carriageways, and rendering a staircase as one
	 * would be worse than skipping it.
	 */
	static bool IsDrivableHighway(const FTerraDyneOSMWay& Way);

	/**
	 * Storeys for a building way. Prefers building:levels; falls back to the height tag in metres
	 * divided by a 3 m storey; otherwise DefaultLevels. Always at least 1.
	 */
	static int32 ResolveBuildingLevels(const FTerraDyneOSMWay& Way, int32 DefaultLevels = 2);

	/**
	 * Carriageway width in metres. Prefers an explicit width tag, then lanes x 3.5 m, then a
	 * per-class default. Never returns zero, so a road is always buildable.
	 */
	static float ResolveHighwayWidthMeters(const FTerraDyneOSMWay& Way);

	/** True for the extensions this reader accepts. */
	static bool IsSupportedExtension(const FString& Extension);
};
