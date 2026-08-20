// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IO/TerraDyneGeoTIFFReader.h"

class ACesiumGeoreference;

/**
 * Turns a decoded GeoTIFF raster into a queryable "what's the ground height at this Unreal world
 * position" source, via the level's CesiumGeoreference for the real-world <-> Unreal transform.
 *
 * Deliberately a plain C++ class, not a UObject: it has no per-frame Tick, no reason to be garbage
 * collected independently of whoever owns it, and keeping it UObject-free is what would let a
 * future version decode/sample it off the game thread without a redesign.
 */
class TERRADYNE_API FTerraDyneGeoTIFFHeightSource
{
public:
	/**
	 * Consumes Raster (moved in — this object owns the elevation data for its lifetime). Resolves
	 * (or creates, via ACesiumGeoreference::GetDefaultGeoreference) the level's georeference, and if
	 * bEstablishGeoreferenceOrigin recenters it to the raster's own geographic centre -- a freshly
	 * auto-created georeference otherwise defaults to lon/lat (0,0), which is useless here.
	 *
	 * HeadroomFraction reserves that fraction of the resolved ZScale below/above the raster's real
	 * elevation range, mirroring TerraDyneLayers::ProceduralFloor/ProceduralRelief's headroom idea,
	 * so sculpting still has room to raise/lower imported terrain without clipping against [0,1].
	 * OverrideZScaleOrZero forces a specific ZScale instead of the auto-computed one; 0 = auto.
	 */
	bool Initialize(
		FTerraDyneGeoTIFFRaster&& Raster,
		UWorld* World,
		bool bEstablishGeoreferenceOrigin,
		float HeadroomFraction,
		float OverrideZScaleOrZero,
		FString& OutError);

	/** The raster's footprint in Unreal world XY (cm), for enumerating which chunks it covers. */
	FBox2D GetWorldFootprintXY() const { return WorldFootprintXY; }

	/** ZScale to assign to a chunk populated from this source, so BaseBuffer's [0,1] band matches. */
	float GetZScale() const { return ZScale; }

	/**
	 * Bilinear-sampled, [0,1]-normalized height at a world XY (cm, relative to the TerraDyne
	 * manager's own actor location -- callers pass the same world-space values chunk generation
	 * already works in). False if WorldX/WorldY falls outside the raster's coverage.
	 */
	bool TrySampleWorldXY(double WorldX, double WorldY, float& OutNormalized01) const;

private:
	FTerraDyneGeoTIFFRaster Raster;
	TWeakObjectPtr<ACesiumGeoreference> Georeference;

	FBox2D WorldFootprintXY = FBox2D(ForceInit);

	double ElevationFloorMeters = 0.0;
	double ElevationRangeMeters = 1.0;
	float ZScale = 8000.0f;

	/** World XY (cm) -> the raster's own CRS space (degrees if geographic, metres if UTM). */
	bool WorldXYToRasterCrs(double WorldX, double WorldY, double& OutCrsX, double& OutCrsY) const;
};
