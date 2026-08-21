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
	 * Unreal Z, in cm, that an imported chunk actor must sit at for its terrain to land at the DEM's
	 * true real-world altitude.
	 *
	 * A chunk builds its vertices at local Z = normalizedHeight * ZScale, i.e. always from zero up.
	 * On its own that pins the bottom of the elevation band to whatever Z the chunk actor happens to
	 * be at -- so a Himalayan DEM whose terrain genuinely sits 500-1500 m above sea level renders as
	 * a 0-1000 m slab starting at the player's feet, and a sea-level DEM renders identically. The
	 * altitude information is simply discarded.
	 *
	 * Initialize() puts the Cesium georeference origin at the DEM's own centre INCLUDING its real
	 * elevation, so Unreal Z = 0 means "the altitude at the middle of this DEM". This offset then
	 * places the band correctly relative to that: terrain below the centre elevation goes below Z=0,
	 * terrain above goes above, and the player standing at Z ~ 0 is standing on the ground rather
	 * than under a kilometre of it.
	 */
	double GetChunkWorldZOffset() const { return (ElevationFloorMeters - OriginElevationMeters) * 100.0; }

	/** Real-world elevation, in metres, that Unreal Z = 0 corresponds to for this import. */
	double GetOriginElevationMeters() const { return OriginElevationMeters; }

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

	/**
	 * Raw elevation in metres at a pixel, or Fallback when that pixel is NoData.
	 *
	 * Used to pick the origin altitude, which must not be a NoData sentinel -- taking -9999 as
	 * "the altitude of Unreal Z 0" would put the entire import ten kilometres into the sky.
	 */
	double SampleElevationMetersAtPixelOrFallback(double Col, double Row, double Fallback) const;

	/** Elevation at the DEM's centre; the georeference origin is placed here, so this is Unreal Z 0. */
	double OriginElevationMeters = 0.0;

	double ElevationFloorMeters = 0.0;
	double ElevationRangeMeters = 1.0;
	float ZScale = 8000.0f;

	/** World XY (cm) -> the raster's own CRS space (degrees if geographic, metres if UTM). */
	bool WorldXYToRasterCrs(double WorldX, double WorldY, double& OutCrsX, double& OutCrsY) const;
};
