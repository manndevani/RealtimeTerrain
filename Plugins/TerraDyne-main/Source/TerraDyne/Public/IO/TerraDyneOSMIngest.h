// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IO/TerraDyneOSMParser.h"

class UWorld;

/** What an OSM import should bring in, and how much of it. */
struct TERRADYNE_API FTerraDyneOSMImportOptions
{
	bool bImportBuildings = true;
	bool bImportRoads = true;

	/** Storeys for a building way that tags neither building:levels nor height. */
	int32 DefaultBuildingLevels = 2;

	/**
	 * Hard ceiling on features created per import. A city-sized extract holds hundreds of thousands
	 * of ways, and spawning that many actors would wedge the editor with no way back. Whatever is
	 * dropped is reported in the summary rather than silently truncated.
	 */
	int32 MaxFeatures = 2000;
};

/** What one import actually did. */
struct TERRADYNE_API FTerraDyneOSMImportSummary
{
	bool bSuccess = false;
	FString ErrorMessage;

	int32 BuildingsCreated = 0;
	int32 RoadsCreated = 0;

	/** Ways that classified as a building or road but could not be built -- too few usable points, unresolved node refs. */
	int32 BuildingsSkipped = 0;
	int32 RoadsSkipped = 0;

	/** Features dropped because MaxFeatures was reached. Non-zero means the import was truncated. */
	int32 DroppedToLimit = 0;

	FString ToDisplayString() const;
};

/**
 * Geometry helpers turning parsed OSM ways into the shapes this project's actors expect.
 *
 * Kept pure and world-free on purpose. The interesting decisions here -- which way a footprint has
 * to wind, where its origin sits, which duplicate points to drop -- are exactly the ones that go
 * wrong silently and are invisible to a "did it import" check, so they need to be testable without
 * a map, a Cesium georeference, or a spawned actor.
 */
class TERRADYNE_API FTerraDyneOSMIngest
{
public:
	/**
	 * Shoelace signed area of a closed polygon in XY. Positive is counter-clockwise.
	 * Z is ignored; footprints are flat by construction.
	 */
	static double ComputeSignedArea2D(const TArray<FVector>& Points);

	/**
	 * Forces Points counter-clockwise, reversing them if needed.
	 *
	 * ATerraDyneBuildingActor::ResetToDefaultFootprint winds counter-clockwise deliberately, and
	 * wall generation and the terrain pad both read that ordering, so a clockwise footprint builds
	 * a building inside-out. This cannot be decided from OSM's own convention either: this project
	 * georeferences through Cesium in East-South-Up, so latitude increases toward -Y and the
	 * conversion flips the handedness of every ring on the way in. Normalising after conversion, in
	 * Unreal space, is the only ordering that is actually correct.
	 */
	static void NormalizeFootprintWinding(TArray<FVector>& Points);

	/**
	 * Turns a way's world-space points into an actor origin plus footprint points local to it.
	 *
	 * Drops OSM's repeated closing node and any consecutive duplicates, flattens to Z=0 (the
	 * building carries its own height), centres the origin on the footprint centroid so moving the
	 * building is one transform rather than a per-point rewrite, and normalises winding. Returns an
	 * empty array when fewer than three distinct corners survive.
	 */
	static TArray<FVector> BuildFootprintLocal(const TArray<FVector>& WorldPoints, FVector& OutOrigin);

	/**
	 * Cleans a road centreline: drops consecutive duplicates and, if the way is closed, its
	 * repeated final node. Returns fewer than two points when nothing usable is left.
	 */
	static TArray<FVector> BuildRoadCentreline(const TArray<FVector>& WorldPoints);

	/**
	 * Resolves a way's node ids to world positions through the supplied converter.
	 * Returns false if any node id is missing from the document.
	 */
	static bool ResolveWayWorldPoints(
		const FTerraDyneOSMData& Data,
		const FTerraDyneOSMWay& Way,
		TFunctionRef<FVector(double /*Lon*/, double /*Lat*/)> LonLatToWorld,
		TArray<FVector>& OutWorldPoints);
};
