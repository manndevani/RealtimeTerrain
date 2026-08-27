// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IO/TerraDyneRasterLayers.h"
#include "GameFramework/Actor.h"
#include "Core/TerraDyneWorldTypes.h"
#include "World/TerraDyneTileData.h"
#include "Core/TerraDyneSaveGame.h"
#include "TerraDyneManager.generated.h"

// Forward Declarations
class ATerraDyneChunk;
class ALandscapeProxy;
class ULandscapeComponent;
class UMaterialInterface;
class ULandscapeLayerInfoObject;
class USceneComponent;
class UTerraDyneWorldPreset;
class ATerraDyneEditController;
class FTerraDyneGeoTIFFHeightSource;
class ACesium3DTileset;
class UCesiumUrlTemplateRasterOverlay;
class ACesiumCartographicPolygon;

/** Buffers snapshot for a single chunk before a stroke begins. */
struct FTerraDyneChunkSnapshot
{
	FIntPoint Coordinate;
	TArray<float> SculptBuffer;
};

/** One undo/redo history entry — covers all chunks touched by a single stroke. */
struct FTerraDyneUndoEntry
{
	TMap<FIntPoint, FTerraDyneChunkSnapshot> Snapshots;
};

USTRUCT(BlueprintType)
struct FTerraDyneGPUStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	FString AdapterName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	bool bIsCudaAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	int32 DedicatedVideoMemory = 0; // in MB

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	FString ComputeBackend; // GPU+CPU Hybrid or CPU Fallback
};

USTRUCT(BlueprintType)
struct FTerraDyneLandscapeMigrationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bHideSourceLandscape = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bClearExistingChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bPauseStreamingDuringImport = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bImportWeightLayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bCaptureLayerMappings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bAdoptLandscapeMaterialAsMasterMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bTransferPlacedFoliage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bTransferredFoliageFollowsTerrain = true;
};

/** Which terrain source a world uses. Sandbox is the default -- current behaviour, unchanged. */
UENUM(BlueprintType)
enum class ETerraDyneWorldMode : uint8
{
	Sandbox  UMETA(DisplayName = "Sandbox (Procedural)"),
	Imported UMETA(DisplayName = "Imported (GeoTIFF)")
};

USTRUCT(BlueprintType)
struct FTerraDyneGeoTIFFImportOptions
{
	GENERATED_BODY()

	/**
	 * Recentre the level's CesiumGeoreference origin to the imported GeoTIFF's own geographic
	 * centre. Turn off if the level already has other Cesium content anchored to a specific origin
	 * this import should sit relative to instead of recentring around itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	bool bEstablishGeoreferenceOrigin = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	bool bClearExistingChunks = true;

	/**
	 * Imported worlds are a bounded island by design: outskirts growth is force-disabled at import
	 * time (see ImportGeoTIFFInternal) rather than left to grow procedural terrain past the DEM's
	 * footprint, since procedural's fixed elevation band essentially never matches a real DEM's
	 * range and the two would meet in a visible cliff. Off by default here for the same reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	bool bDisableOutskirtsGrowth = true;

	/** 0 = auto-compute from the GeoTIFF's own elevation range plus HeadroomFraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings", meta = (ClampMin = "0.0"))
	float OverrideZScale = 0.0f;

	/** Fraction of the resolved ZScale reserved below/above the GeoTIFF's real elevation range, so sculpting still has [0,1] headroom instead of clipping immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings", meta = (ClampMin = "0.0", ClampMax = "0.4"))
	float HeadroomFraction = 0.1f;
};

/**
 * One patch of ground an actor wants levelled to a height, with a skirt blending back to natural
 * terrain. Applied through the same non-destructive pad layer buildings use, so it never touches
 * the sculpt buffer and disappears cleanly when its owner does.
 */
USTRUCT()
struct FTerraDynePadStamp
{
	GENERATED_BODY()

	/** Footprint in world XY. Needs at least three points. */
	UPROPERTY()
	TArray<FVector2D> PolygonWorldXY;

	/** World height the ground inside the footprint is pulled to. */
	UPROPERTY()
	float TargetWorldZ = 0.0f;

	/** Width in cm of the blend from the footprint edge back to natural ground. */
	UPROPERTY()
	float Falloff = 250.0f;
};

UCLASS(Blueprintable)
class TERRADYNE_API ATerraDyneManager : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneManager();
	// Declared (defined in the .cpp, where FTerraDyneGeoTIFFHeightSource is a complete type) so the
	// compiler never tries to inline-generate a destructor for ActiveGeoTIFFHeightSource's TUniquePtr
	// against the forward declaration alone.
	virtual ~ATerraDyneManager() override;
	// UHT's own auto-generated vtable-helper constructor (in TerraDyneManager.gen.cpp) hits the same
	// incomplete-type problem -- that file has no visibility into FTerraDyneGeoTIFFHeightSource at
	// all. Declaring this ourselves stops UHT from generating one; defined in the .cpp instead.
	ATerraDyneManager(FVTableHelper& Helper);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TObjectPtr<USceneComponent> SceneRoot;

	//--- CONFIGURATION ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	TObjectPtr<ALandscapeProxy> TargetLandscapeSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Import Settings")
	float GlobalChunkSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Config")
	TSubclassOf<ATerraDyneChunk> ChunkClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Config")
	ETerraDyneLayer ActiveLayer = ETerraDyneLayer::Sculpt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterMaterial, Category = "TerraDyne|Config")
	TObjectPtr<UMaterialInterface> MasterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Tools")
	TObjectPtr<UMaterialInterface> HeightBrushMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	FTerraDyneLandscapeMigrationOptions LandscapeMigrationOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_LandscapeMigrationState, Category = "TerraDyne|Migration")
	FTerraDyneLandscapeMigrationState LandscapeMigrationState;

	/**
	 * Absolute path to the GeoTIFF to import when WorldMode is Imported. Empty until something
	 * (the runtime file-picker popup, typically) sets it -- BeginPlay does nothing for Imported mode
	 * until this is non-empty, and ImportGeoTIFF() can also be called directly with an explicit path
	 * after BeginPlay to import outside the bootstrap flow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	FString GeoTIFFImportPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	FTerraDyneGeoTIFFImportOptions GeoTIFFImportOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_GeoTIFFImportState, Category = "TerraDyne|Import Settings")
	FTerraDyneGeoTIFFImportState GeoTIFFImportState;

	/**
	 * Which of ImportedChunkCoords' entries are genuine imported terrain vs. procedural: Landscape
	 * import and GeoTIFF import both populate ImportedChunkCoords the same way (for the shared
	 * authored-vs-procedural gating in ShouldAllowChunkCoord/population rules), but only GeoTIFF
	 * import wants LoadOrSpawnChunk to sample ActiveGeoTIFFHeightSource for a streamed-in chunk.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_GeoTIFFImportState, Category = "TerraDyne|Import Settings")
	ETerraDyneChunkOrigin WorldOriginKind = ETerraDyneChunkOrigin::Procedural;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Framework")
	TObjectPtr<UTerraDyneWorldPreset> WorldPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Procedural")
	FTerraDyneProceduralWorldSettings ProceduralWorldSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Procedural")
	TArray<FTerraDyneBiomeOverlay> BiomeOverlays;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Population")
	TArray<FTerraDynePopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneAISpawnZone> AISpawnZones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneBuildPermissionZone> BuildPermissionZones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	bool bRefreshNavMeshOnTerrainChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	bool bRefreshNavMeshOnPopulationChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	float NavigationDirtyBoundsPadding = 300.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Population")
	TArray<FTerraDynePersistentPopulationEntry> PersistentPopulationEntries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Procedural")
	TArray<FTerraDyneProceduralChunkState> ProceduralChunkStates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bAutoImportAtRuntime = false;

	/**
	 * Sandbox (default) is today's behaviour, unchanged -- see bSpawnDefaultChunksOnBeginPlay below.
	 * Imported spawns nothing at BeginPlay until GeoTIFFImportPath is set and ImportGeoTIFF() has
	 * run (either because GeoTIFFImportPath was already set before BeginPlay, or via a later call --
	 * see the runtime file-picker flow).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	ETerraDyneWorldMode WorldMode = ETerraDyneWorldMode::Sandbox;

	/**
	 * When true (the default), BeginPlay spawns nothing at all -- no default sandbox chunk, no
	 * auto-import from GeoTIFFImportPath -- regardless of WorldMode or bSpawnDefaultChunksOnBeginPlay
	 * below. The world sits idle until something explicitly starts it: the runtime mode-choice
	 * widget calling StartSandboxMode()/ImportGeoTIFF() through ATerraDyneEditController, or a level/
	 * Blueprint that wants the old unconditional-fire behaviour turning this off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bShowWorldModeChoiceOnBeginPlay = true;

	/**
	 * True from BeginPlay until the player actually picks Sandbox or Import.
	 *
	 * bShowWorldModeChoiceOnBeginPlay only stops BeginPlay itself from spawning; it does NOT stop
	 * UpdateStreaming, which runs off Tick every ~0.25s and fills a ChunkLoadRadius diamond around
	 * the player regardless of whether anything has been decided. That is why an undecided world
	 * still came up full of procedural sandbox terrain (a radius of 4 yields 2*4*4+2*4+1 = 41 chunks)
	 * a quarter-second after a BeginPlay that had correctly spawned nothing. Streaming is suppressed
	 * while this is set, so "spawns nothing" means nothing.
	 *
	 * Replicated because the UI that presents the choice runs on the client, and the tool panel keys
	 * its own visibility off this so the choice is genuinely the first and only thing on screen.
	 */
	// Defaults true (not false): other actors' BeginPlay order relative to this one is not
	// guaranteed, so anything that reads this before this actor's own BeginPlay has run must still
	// see the safe "awaiting" value, not a stale "not awaiting" default. BeginPlay explicitly clears
	// it on every path that resolves the decision itself (resume, legacy auto-import, legacy
	// auto-sandbox); the undecided path leaves this default true untouched.
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "TerraDyne|Bootstrap")
	bool bAwaitingWorldModeChoice = true;

	/** Clears bAwaitingWorldModeChoice and lets streaming resume. Safe to call more than once. */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Bootstrap")
	void MarkWorldModeChosen();

	/**
	 * Fires once, exactly when MarkWorldModeChosen() actually transitions bAwaitingWorldModeChoice
	 * from true to false. Anything that must not build world content ahead of the player's choice --
	 * the showcase/demo sequence in particular -- binds here and defers its own startup until this
	 * fires, rather than either racing this actor's BeginPlay or forcing the choice itself.
	 */
	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneWorldModeChosenDelegate OnWorldModeChosen;

	//--- MULTI-LAYER RASTER IMPORT ---

	/**
	 * One slot per ETerraDyneRasterLayer, indexed by that enum's value.
	 *
	 * Sized once at construction and never resized, so a layer's slot is always addressable whether
	 * or not anything has been imported into it.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_RasterLayerSlots, BlueprintReadOnly, Category = "TerraDyne|Raster")
	TArray<FTerraDyneRasterLayerSlot> RasterLayerSlots;

	UFUNCTION()
	void OnRep_RasterLayerSlots();

	/**
	 * Validates a raster and records it in its layer slot. Reads only the header, so it returns
	 * immediately even for a very large file -- the pixels are decoded later, by LoadRasterLayers.
	 *
	 * Enforces the two rules that keep a layer set coherent:
	 *   1. Elevation must be imported before any other layer. Everything else is checked for
	 *      agreement against the elevation grid, so without it there is nothing to check against.
	 *   2. Every later layer must sit on the same grid as the elevation raster -- same size, same
	 *      pixel scale, same CRS, origin within the half-pixel grid-convention allowance. This is
	 *      what stops a DEM of one valley being combined with a hillshade of another.
	 *
	 * Returns false with a human-readable OutError on any violation; the slot is left untouched.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool ImportRasterLayer(ETerraDyneRasterLayer Layer, const FString& FilePath, FString& OutError);

	/**
	 * Clears one layer slot.
	 *
	 * Unloading Elevation also unloads every other layer: they were only ever admitted because they
	 * matched that specific DEM's grid, so keeping them once it is gone would leave a set validated
	 * against nothing, and the next DEM imported could be somewhere else entirely.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	void UnloadRasterLayer(ETerraDyneRasterLayer Layer);

	/** Clears every layer slot and any terrain built from them. */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	void UnloadAllRasterLayers();

	/**
	 * Decodes the imported layers and builds the world from them -- the expensive step the import
	 * calls deliberately skip.
	 *
	 * Elevation drives the terrain itself (the existing GeoTIFF import path). Returns false with
	 * OutError if no elevation layer has been imported, since there is no world to build without it.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool LoadRasterLayersIntoScene(FString& OutError);

	/** Dynamic raster layers imported by user (in addition to the primary Elevation DEM slot). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "TerraDyne|Raster")
	TArray<FTerraDyneDynamicRasterLayer> DynamicRasterLayers;

	/**
	 * Inspects, auto-detects, checks for duplicates, and adds a dynamic raster layer.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool AddDynamicRasterLayer(const FString& FilePath, FTerraDyneDynamicRasterLayer& OutLayer, FString& OutError);

	/**
	 * Removes a dynamic layer by index.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool RemoveDynamicRasterLayer(int32 LayerIndex);

	/**
	 * Clears all dynamic layers.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	void ClearAllDynamicRasterLayers();

	/**
	 * Loads and applies a dynamic raster layer as the active visual overlay on the terrain.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool LoadDynamicRasterLayer(int32 LayerIndex, FString& OutError);

	/**
	 * Unloads a dynamic raster layer from the scene.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	void UnloadDynamicRasterLayer(int32 LayerIndex);

	/** True when a layer slot holds a validated raster. */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Raster")
	bool IsRasterLayerImported(ETerraDyneRasterLayer Layer) const;

	/** True when a specific layer is currently loaded and active in the scene */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Raster")
	bool IsRasterLayerLoadedInScene(ETerraDyneRasterLayer Layer) const;

	/** Loads a specific imported raster layer into the scene */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool LoadSpecificRasterLayer(ETerraDyneRasterLayer Layer, FString& OutError);

	/** Unloads a specific raster layer from the scene without clearing its imported file */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Raster")
	bool UnloadSpecificRasterLayer(ETerraDyneRasterLayer Layer, FString& OutError);

	/** The elevation slot, which gates everything else. */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Raster")
	bool HasElevationLayer() const { return IsRasterLayerImported(ETerraDyneRasterLayer::Elevation); }

	/** Returns 0.0 to 1.0 progress fraction of imported DEM chunks loaded into scene */
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Raster")
	float GetGeoTIFFLoadProgress(int32& OutLoaded, int32& OutTotal) const;

	/** Access coordinates covered by the imported GeoTIFF */
	const TSet<FIntPoint>& GetImportedChunkCoords() const { return ImportedChunkCoords; }

	/** Access currently loaded chunks */
	const TMap<FIntPoint, ATerraDyneChunk*>& GetActiveChunkMap() const { return ActiveChunkMap; }

	/** True if ActiveGeoTIFFHeightSource is decoded and ready for sampling */
	bool HasElevationHeightSource() const;

	/** Currently active visual overlay texture (e.g. Color Relief / Hillshade) */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "TerraDyne|Raster")
	TObjectPtr<class UTexture2D> ActiveGeoTIFFVisualOverlayTexture;

	/** Decoded pixel buffer of the active visual overlay for instant chunk color sampling */
	TArray<FColor> ActiveGeoTIFFVisualOverlayPixels;
	int32 ActiveGeoTIFFVisualOverlayWidth = 0;
	int32 ActiveGeoTIFFVisualOverlayHeight = 0;

	/** Active visual overlay raster layer and display name */
	ETerraDyneRasterLayer ActiveVisualOverlayLayer = ETerraDyneRasterLayer::Elevation;
	FString ActiveVisualOverlayName;

	bool HasActiveVisualOverlay() const { return ActiveGeoTIFFVisualOverlayPixels.Num() > 0 && ActiveVisualOverlayLayer != ETerraDyneRasterLayer::Elevation; }
	ETerraDyneRasterLayer GetActiveVisualOverlayLayer() const { return ActiveVisualOverlayLayer; }
	const FString& GetActiveVisualOverlayName() const { return ActiveVisualOverlayName; }

	/** Updates the visual layer coloring across all active chunks */
	void ApplyVisualOverlayToAllChunks();

	//--- TILED CESIUM IMPORT ---
	// Displays disk-based Cesium tile pyramids (quantized-mesh terrain, XYZ imagery) alongside
	// TerraDyne's own chunk terrain, entirely offline via file:/// URLs -- ACesium3DTileset's
	// FromUrl source and UCesiumUrlTemplateRasterOverlay never touch CesiumIonServer or open a
	// socket for a local path. This is a separate rendered actor, not a height/overlay source
	// TerraDyne's own chunks sample from (see FTerraDyneGeoTIFFHeightSource for that).
	//
	// DEM-only: both entry points refuse to run in a Sandbox (procedural) world -- a tiled real-world
	// pyramid dropped next to procedurally generated terrain would sit at the wrong scale and the
	// wrong place relative to it, since Sandbox has no georeferenced footprint to align against.
	// Gated on the same WorldOriginKind/HasElevationLayer check the GeoTIFF raster UI already uses
	// (see STerraDynePanel::GetGeoTIFFSectionVisibility) so the two stay consistent.

	/**
	 * Points a Cesium3DTileset at a local quantized-mesh pyramid: a folder containing layer.json
	 * and its {z}/{x}/{y}.terrain tiles. Spawns the tileset actor on first call; later calls
	 * repoint the same actor at a new folder.
	 *
	 * OutCenterLonLat/OutSpanDegrees describe the pyramid's own geographic footprint (its deepest
	 * zoom level's tile index range, converted to lon/lat) so the caller can fly the camera there --
	 * left at (0,0)/0 if the footprint couldn't be determined, which is not itself a failure.
	 *
	 * When bCutOutBackdropForTiledImport is on (the default), also cuts a hole -- via a
	 * UCesiumPolygonRasterOverlay clip on a rectangle matching this footprint -- in every OTHER
	 * ACesium3DTileset already in the world (e.g. a placed Cesium World Terrain used as the level's
	 * default globe backdrop), so it stops rendering real terrain geometry for the same ground this
	 * import now covers instead of visibly overlapping/z-fighting with it. See
	 * SetCutOutBackdropForTiledImport to turn this off.
	 *
	 * Returns false with OutError if the world is Sandbox rather than Imported/DEM, or if
	 * PyramidFolder has no layer.json.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|TiledImport")
	bool ImportCesiumTerrainTileset(const FString& PyramidFolder, FString& OutError, FVector2D& OutCenterLonLat, double& OutSpanDegrees);

	/**
	 * Adds (or repoints) a URL-template raster overlay reading a local XYZ PNG pyramid -- files laid
	 * out on disk as {z}/{x}/{y}.png under PyramidFolder, WebMercator projection -- onto the tileset
	 * spawned by ImportCesiumTerrainTileset, spawning a bare ellipsoid tileset first if none exists
	 * yet so the overlay has something to drape onto. Requested via Cesium's "{reverseY}" template
	 * token, not "{y}": Cesium's own convention for "{y}" is TMS row-ordering (0 = south), while a
	 * standard XYZ/slippy-map export like these (0 = north) needs "{reverseY}" -- using "{y}" here
	 * would silently request every row from the wrong end of the pyramid.
	 *
	 * The zoom range is read from PyramidFolder's own numeric subfolder names, not hardcoded.
	 * OutCenterLonLat/OutSpanDegrees are the same kind of footprint result as
	 * ImportCesiumTerrainTileset's, computed from this pyramid's own tiles.
	 *
	 * Returns false with OutError if the world is Sandbox rather than Imported/DEM, or if no
	 * numeric zoom subfolders are found.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|TiledImport")
	bool ImportCesiumImageryOverlay(const FString& PyramidFolder, FString& OutError, FVector2D& OutCenterLonLat, double& OutSpanDegrees);

	/** The tileset actor spawned by either tiled-import call above, or null if neither has run. */
	ACesium3DTileset* GetActiveTiledCesiumTileset() const { return ActiveTiledCesiumTileset; }

	/** The imagery overlay component spawned by ImportCesiumImageryOverlay, or null if it hasn't run. */
	UCesiumUrlTemplateRasterOverlay* GetActiveTiledCesiumOverlay() const { return ActiveTiledCesiumOverlay; }

	/**
	 * Turns the backdrop cutout described on ImportCesiumTerrainTileset on/off, live -- switching it
	 * off removes any clip this manager already applied and leaves every other tileset fully intact;
	 * switching it back on re-applies the clip for whatever footprint was last imported. Off is the
	 * right call if the computed footprint is wrong/approximate (a bad zoom-level scan, say) and the
	 * cutout would carve a hole in the wrong place.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|TiledImport")
	void SetCutOutBackdropForTiledImport(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "TerraDyne|TiledImport")
	bool GetCutOutBackdropForTiledImport() const { return bCutOutBackdropForTiledImport; }

	/**
	 * Vertical offset (metres) applied to the imported tiled Cesium actor (terrain or the bare
	 * ellipsoid an imagery-only import spawns) -- lets the user nudge the imported layer up/down to
	 * work around a residual vertical mismatch against the backdrop, or z-fighting right at the
	 * cutout edge. Absolute, not cumulative: calling this twice with the same value is a no-op, not
	 * a double lift.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|TiledImport")
	void SetTiledCesiumHeightOffset(double OffsetMeters);

	UFUNCTION(BlueprintPure, Category = "TerraDyne|TiledImport")
	double GetTiledCesiumHeightOffset() const { return TiledCesiumHeightOffsetMeters; }

	/** Spawns or loads a chunk at the given grid coordinate */
	void LoadOrSpawnChunk(FIntPoint Coord);

	/** Unloads a chunk at the given grid coordinate */
	void UnloadChunk(FIntPoint Coord);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSpawnDefaultChunksOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSetupDefaultLightingOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSpawnShowcaseOnBeginPlay = false;

	//--- RUNTIME API ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Interaction")
	void ApplyGlobalBrush(
		FVector WorldLocation,
		float Radius,
		float Strength,
		ETerraDyneBrushMode BrushMode,
		float FlattenHeight = 0.f);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Interaction")
	void ApplyGlobalNoise(float Strength, float Frequency, float Seed);

	/**
	 * Swap the terrain material for the whole world — every loaded chunk plus any that stream in
	 * afterwards. Intended for biome switching: author one material instance per biome (desert,
	 * tundra, etc.) and swap between them.
	 *
	 * Server only. Clients pick the change up through OnRep_MasterMaterial, which is why the
	 * authority has to apply it explicitly here — a ReplicatedUsing notify does not fire on the
	 * machine that set the value.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Config")
	void SetMasterMaterial(UMaterialInterface* NewMaterial);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Query")
	ATerraDyneChunk* GetChunkAtLocation(FVector WorldLocation) const;

	/** Returns all active chunks whose AABB overlaps the given world-space circle. */
	TArray<ATerraDyneChunk*> GetChunksInRadius(FVector WorldLocation, float Radius);

	/** Returns all active chunks whose footprint overlaps the given world-space XY box. */
	TArray<ATerraDyneChunk*> GetChunksInBoxXY(const FBox2D& WorldAreaXY) const;

	/**
	 * Terrain height at a world XY, ignoring any building pad.
	 * Returns false when no loaded chunk covers that spot.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Query")
	bool GetNaturalTerrainHeightAtXY(FVector2D WorldXY, float& OutWorldZ) const;

	//--- Buildings ---

	/**
	 * Buildings register themselves so the manager can rebuild terrain pads without having to
	 * sweep the level for actors. Weak pointers: a building destroyed by any route just drops out.
	 */
	void RegisterBuilding(class ATerraDyneBuildingActor* Building);
	void UnregisterBuilding(class ATerraDyneBuildingActor* Building);

	/**
	 * Rebuilds the pad layer for every loaded chunk overlapping WorldAreaXY.
	 *
	 * Callers pass the union of a building's OLD and NEW footprint bounds, so the chunks it has
	 * just left are recomputed too — otherwise a moved building leaves its pad behind.
	 */
	void RefreshBuildingPads(const FBox2D& WorldAreaXY);

	/** Rebuilds one chunk's pad layer from scratch. Cheap no-op when no building overlaps it. */
	void RefreshBuildingPadsForChunk(ATerraDyneChunk* Chunk);

	/**
	 * Pads contributed by actors that are not buildings — roads being the reason this exists.
	 *
	 * Buildings each own one footprint, so they are registered as actors and their polygon is asked
	 * for on demand. A road is a long strip that has to step its target height along its length, so
	 * it supplies many small quads instead. Both end up in the same PadWeightBuffer, composed by
	 * whichever stamp wants the ground flattest.
	 *
	 * Keyed by owner: setting again replaces that owner's stamps rather than accumulating them.
	 */
	void SetExternalPadStamps(AActor* PadOwner, const TArray<FTerraDynePadStamp>& Stamps);
	void ClearExternalPadStamps(AActor* PadOwner);

	/** World-space XY bounds covering every stamp an owner contributed. Invalid when it has none. */
	FBox2D GetExternalPadBounds(AActor* PadOwner) const;

	/** World-space XY bounds of a chunk. */
	FBox2D GetChunkBoundsXY(const ATerraDyneChunk* Chunk) const;

	/** Returns the chunk at the given grid coordinate, or nullptr. */
	ATerraDyneChunk* GetChunkAtCoord(FIntPoint Coord) const;

	void RegisterChunk(ATerraDyneChunk* Chunk);
	void UnregisterChunk(ATerraDyneChunk* Chunk);

	void BeginStroke(FVector WorldLocation, float Radius, APlayerController* Controller);
	void CommitStroke(APlayerController* Controller);
	void RestoreSnapshots(const FTerraDyneUndoEntry& Entry);
	void Undo(APlayerController* Controller);
	void Redo(APlayerController* Controller);

	/** Server → All Clients: broadcast a brush application so every client updates visuals. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyBrush(const FTerraDyneBrushParams& Params);

	/** Server → All Clients: broadcast global noise application to all clients. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyGlobalNoise(float Strength, float Frequency, float Seed);

	/** Server → All Clients: broadcast building pad refresh to all clients. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_RefreshBuildingPads(FVector2D MinXY, FVector2D MaxXY);


	/** Send every live chunk's state to one controller, to catch up a late joiner. Server only. */
	void SendFullSyncToController(ATerraDyneEditController* Controller);

	/**
	 * Server → All Clients: replay one chunk's sculpt state after an undo/redo.
	 *
	 * Sent as sparse (index, value) pairs in batches rather than the whole buffer. A full sculpt
	 * buffer is Resolution^2 floats — 65,536 bytes at the default 128 — which overflows Unreal's
	 * 65,536-byte maximum bunch size by the RPC header alone, and the send is dropped with only an
	 * "Attempted to send bunch exceeding max allowed size" error on the server. The client never
	 * hears about it, so undo appeared to work on the host and do nothing everywhere else.
	 *
	 * Sculpt buffers are mostly zeroes (only edited ground is non-zero), so the sparse form is
	 * typically a tiny fraction of the raw array, and batching bounds the worst case.
	 *
	 * bFirstBatch clears the client's buffer before applying; bFinalBatch triggers the mesh rebuild.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SyncChunkSculpt(FIntPoint Coord, const TArray<int32>& Indices, const TArray<float>& Values, bool bFirstBatch, bool bFinalBatch);

	/** Splits a chunk's sculpt buffer into safely-sized sparse batches and multicasts them. */
	void SendChunkSculptSync(class ATerraDyneChunk* Chunk);

	/** Returns true if a stroke is pending for the given controller. */
	bool HasPendingStroke(APlayerController* Controller) const
	{
		return PendingStroke.IsSet() && PendingStrokeOwner == Controller;
	}

	/** Returns true if undo is available for this player. */
	bool CanUndo(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
		return Stack && Stack->Num() > 0;
	}

	/** Returns true if redo is available for this player. */
	bool CanRedo(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
		return Stack && Stack->Num() > 0;
	}

	/** Depth of undo stack for a player (for testing). */
	int32 GetUndoDepth(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
		return Stack ? Stack->Num() : 0;
	}

	/** Depth of redo stack for a player (for testing). */
	int32 GetRedoDepth(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
		return Stack ? Stack->Num() : 0;
	}

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void RebuildChunkMap();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void SaveWorld(FString SlotName = "TerraDyneSave");

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void LoadWorld(FString SlotName = "TerraDyneSave");

	// Same data as SaveWorld/LoadWorld, but written to/read from an explicit file path instead
	// of the engine's named SaveGame slot system — for exporting/importing a landscape as a
	// standalone file you can move, share, or pick from disk.
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	bool SaveWorldToFile(const FString& FilePath);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	bool LoadWorldFromFile(const FString& FilePath);

	class UTerraDyneSaveGame* BuildSaveGameInstance();
	void ApplyLoadedSaveGame(class UTerraDyneSaveGame* LoadInst, const FString& SourceLabel);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Migration")
	void SetAuthoredChunkCoordinates(const TArray<FIntPoint>& ChunkCoords, bool bWasImportedFromLandscape = true);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Framework")
	void ApplyWorldPreset(UTerraDyneWorldPreset* Preset = nullptr);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid RegisterPersistentActor(
		AActor* Actor,
		ETerraDynePopulationKind Kind = ETerraDynePopulationKind::Prop,
		bool bAllowRegrowth = false,
		float RegrowthDelaySeconds = 0.0f,
		bool bSnapToTerrain = false);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid PlacePersistentActorFromDescriptor(
		const FTerraDynePopulationDescriptor& Descriptor,
		const FTransform& WorldTransform,
		FName SourceRuleId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid PlacePersistentActor(
		TSubclassOf<AActor> ActorClass,
		const FTransform& WorldTransform,
		ETerraDynePopulationKind Kind = ETerraDynePopulationKind::RuntimePlaced,
		bool bSnapToTerrain = true,
		float TerrainOffset = 0.0f,
		bool bAllowRegrowth = false,
		float RegrowthDelaySeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	bool HarvestPersistentPopulation(const FGuid& PopulationId);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	bool SetPersistentPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	void RefreshPopulationForLoadedChunks();

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Population")
	int32 GetPersistentPopulationCount() const { return PersistentPopulationEntries.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Procedural")
	FName GetBiomeTagAtLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Gameplay")
	bool CanBuildAtLocation(FVector WorldLocation, FString& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneAISpawnZone> GetAISpawnZonesAtLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|PCG")
	TArray<FTerraDynePCGPoint> GetPCGSeedPointsForChunk(
		FIntPoint Coord,
		bool bIncludePopulation = true,
		bool bIncludeAISpawnZones = true) const;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneTerrainChangedDelegate OnTerrainChanged;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneFoliageChangedDelegate OnFoliageChanged;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDynePopulationChangedDelegate OnPopulationChanged;

	/** When true, streaming is frozen — no chunks are loaded or unloaded. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "TerraDyne|Streaming")
	bool bStreamingPaused = false;

	//--- STATS ---
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	int32 GetActiveChunkCount() const { return ActiveChunkMap.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	int32 GetTotalVertexCount() const;

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	FTerraDyneGPUStats GetGPUStats() const;

	//--- EDITOR TOOLS ---
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne|Tools")
	void ManualImport();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne|Migration")
	void MigrateLandscapeProject();

	void ImportFromLandscape(ALandscapeProxy* TargetLandscape, bool bHideSource = true);
	void ImportFromLandscapeWithOptions(ALandscapeProxy* TargetLandscape, const FTerraDyneLandscapeMigrationOptions& Options);
	void ResampleLandscapeData(ATerraDyneChunk* Chunk, ULandscapeComponent* SourceComponent);
	void ImportInternal(ALandscapeProxy* Source, const FTerraDyneLandscapeMigrationOptions& Options);
#endif

	/**
	 * Runtime (non-editor-only) entry point for Imported mode: decodes FilePath as a GeoTIFF,
	 * establishes/recentres the level's CesiumGeoreference, marks every chunk coordinate the raster
	 * covers as imported (same ImportedChunkCoords set the Landscape importer uses), and disables
	 * outskirts/edge growth per GeoTIFFImportOptions so nothing procedural grows past the DEM's
	 * footprint. Safe to call from BeginPlay (WorldMode == Imported with GeoTIFFImportPath already
	 * set) or later, e.g. once the runtime file-picker widget resolves a player's chosen file.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Import Settings")
	void ImportGeoTIFF(const FString& FilePath);

	/**
	 * Explicit runtime entry point for Sandbox mode: spawns the default chunk (mirroring what
	 * bSpawnDefaultChunksOnBeginPlay used to fire unconditionally at BeginPlay) the moment something
	 * actually asks for it -- the mode-choice widget's "Sandbox" button, via
	 * ATerraDyneEditController::RequestStartSandboxMode. A no-op if the world already has chunks, so
	 * a second, late, or duplicate call can't stomp on anything already spawned or imported.
	 */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Bootstrap")
	void StartSandboxMode();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Bootstrap")
	void StartDEMImportMode();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_MasterMaterial();

	UFUNCTION()
	void OnRep_LandscapeMigrationState();

	UFUNCTION()
	void OnRep_GeoTIFFImportState();

	UFUNCTION()
	void OnRep_FrameworkConfig();

private:
	UPROPERTY(Transient)
	TMap<FIntPoint, ATerraDyneChunk*> ActiveChunkMap;

	/** Live buildings, for pad rebuilds. Weak so a destroyed building needs no explicit cleanup. */
	TArray<TWeakObjectPtr<class ATerraDyneBuildingActor>> RegisteredBuildings;

	struct FExternalPadEntry
	{
		TWeakObjectPtr<AActor> Owner;
		TArray<FTerraDynePadStamp> Stamps;
	};

	/** Non-building pad contributors. Weak owners, so a destroyed one drops out on the next sweep. */
	TArray<FExternalPadEntry> ExternalPadEntries;

	bool bMaterialsLoaded = false;
	float LODTimer = 0.25f;

	// --- Streaming ---
	// Streaming iterates all player controllers and computes the union of load regions.
	TSet<FIntPoint> PendingLoadQueue;
	TSet<FIntPoint> PendingUnloadQueue;
	TSet<FIntPoint> DirtyChunkSet;
	TSet<FIntPoint> ImportedChunkCoords;

	/**
	 * The decoded GeoTIFF raster for Imported-mode worlds, kept resident for the life of the world
	 * (not decode-and-discard) so chunks that stream in long after the initial import -- possibly
	 * minutes or hours later, far from the player's start position -- can still be populated from it.
	 * Null in Sandbox mode, and null in Imported mode until ImportGeoTIFF() has successfully decoded
	 * a file.
	 */
	TUniquePtr<FTerraDyneGeoTIFFHeightSource> ActiveGeoTIFFHeightSource;

	/** The actor spawned by ImportCesiumTerrainTileset/ImportCesiumImageryOverlay, if either has run. */
	UPROPERTY(Transient)
	TObjectPtr<ACesium3DTileset> ActiveTiledCesiumTileset;

	/** The overlay component spawned by ImportCesiumImageryOverlay, if it has run. */
	UPROPERTY(Transient)
	TObjectPtr<UCesiumUrlTemplateRasterOverlay> ActiveTiledCesiumOverlay;

	/** See SetCutOutBackdropForTiledImport. */
	UPROPERTY(Transient)
	bool bCutOutBackdropForTiledImport = true;

	/** The clip polygon RefreshTiledCesiumBackdropCutout maintains, matching the last-imported footprint. */
	UPROPERTY(Transient)
	TObjectPtr<ACesiumCartographicPolygon> ActiveTiledCesiumClipPolygon;

	/** See SetTiledCesiumHeightOffset. */
	UPROPERTY(Transient)
	double TiledCesiumHeightOffsetMeters = 0.0;

	/** The last footprint either tiled-import call computed -- what RefreshTiledCesiumBackdropCutout clips to. */
	FVector2D LastTiledImportFootprintCenterLonLat = FVector2D::ZeroVector;
	double LastTiledImportFootprintSpanDegrees = 0.0;

	/** Applies (or removes, per bCutOutBackdropForTiledImport) the backdrop cutout described on ImportCesiumTerrainTileset. */
	void RefreshTiledCesiumBackdropCutout();

	/** Applies TiledCesiumHeightOffsetMeters to ActiveTiledCesiumTileset, if one exists. */
	void ApplyTiledCesiumHeightOffset();

	void ImportGeoTIFFInternal(const FString& FilePath, const FTerraDyneGeoTIFFImportOptions& Options);
	bool TryPopulateChunkFromGeoTIFF(ATerraDyneChunk* Chunk, FIntPoint Coord);

	TMap<FGuid, int32> PopulationEntryIndexById;
	TMap<FIntPoint, int32> ProceduralChunkStateIndexByCoord;
	TMap<AActor*, FGuid> PopulationIdByActor;
	TMap<FGuid, TWeakObjectPtr<AActor>> PopulationActorById;
	TSet<FGuid> PopulationDestroyInProgress;
	uint32 LastStreamingHash = 0;
	float PopulationMaintenanceTimer = 0.0f;

	FIntPoint WorldToChunkCoord(const FVector& WorldPos) const;
	void UpdateStreaming(const TArray<FVector>& PlayerPositions);
	void ProcessStreamingQueues(const TArray<FVector>& PlayerPositions);
	void MarkChunkDirty(FIntPoint Coord);
	ATerraDyneChunk* SpawnConfiguredChunk(FIntPoint Coord, int32 OverrideResolution = INDEX_NONE);
	FString GetChunkCachePath(FIntPoint Coord) const;
	void SaveChunkToCache(FIntPoint Coord, const FTerraDyneChunkData& Data);
	bool LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData);
	void ApplyMaterialToActiveChunks();
	void ClearUndoRedoState();
	void DestroyTrackedPopulationActors();
	bool IsImportedChunkCoord(FIntPoint Coord) const;
	bool ShouldAllowChunkCoord(FIntPoint Coord) const;
	int32 GetDistanceFromAuthoredChunks(FIntPoint Coord) const;
	int32 MakeChunkSeed(FIntPoint Coord) const;
	void RebuildWorldStateIndices();
	void EnsureChunkFrameworkState(FIntPoint Coord, ATerraDyneChunk* Chunk);
	FTerraDyneProceduralChunkState* FindProceduralChunkState(FIntPoint Coord);
	const FTerraDyneProceduralChunkState* FindProceduralChunkState(FIntPoint Coord) const;
	FName ResolveBiomeForChunk(FIntPoint Coord, const ATerraDyneChunk* Chunk, TArray<FName>* OutOverlays = nullptr) const;
	void GeneratePopulationForChunk(FIntPoint Coord, ATerraDyneChunk* Chunk);
	void SyncPopulationForChunk(FIntPoint Coord);
	void DespawnPopulationForChunk(FIntPoint Coord);
	AActor* SpawnPopulationActor(const FTerraDynePersistentPopulationEntry& Entry);
	void BindPopulationActor(const FGuid& PopulationId, AActor* Actor);
	UFUNCTION()
	void HandlePopulationActorDestroyed(AActor* DestroyedActor);
	void SetPopulationStateInternal(const FGuid& PopulationId, ETerraDynePopulationState NewState, FName Reason);
	void TickPopulationState(float DeltaTime);
	FTerraDynePopulationDescriptor MakeDescriptorFromActor(
		AActor* Actor,
		ETerraDynePopulationKind Kind,
		bool bAllowRegrowth,
		float RegrowthDelaySeconds,
		bool bSnapToTerrain) const;
	void BroadcastTerrainChanged(const TArray<FIntPoint>& ChangedCoords);
	void BroadcastFoliageChanged(const TArray<FIntPoint>& ChangedCoords);
	void BroadcastPopulationChanged(
		const FTerraDynePersistentPopulationEntry& Entry,
		FName Reason);
	void RefreshNavigationForBounds(const FBox& Bounds, bool bPopulationChange = false);
	float GetSlopeDegreesAtLocation(FVector WorldLocation) const;

	// --- Undo/Redo ---
	// NOTE: Undo/redo entries referencing unloaded (streamed-out) chunks are silently skipped.
	// A future improvement could persist undo snapshots alongside chunk cache data.
	//
	// Weak, not raw, pointers: a player disconnecting mid-stroke (or between BeginStroke and
	// CommitStroke for any other reason) leaves these referencing a controller that can be destroyed
	// out from under them. A raw APlayerController* map key risks a NEW controller later being
	// allocated at the same address and silently inheriting a stale entry; PendingStrokeOwner used to
	// be dereferenced directly in a warning log with no validity check at all, a genuine
	// use-after-free the very next BeginStroke call from a different, still-connected player would hit.
	TMap<TWeakObjectPtr<APlayerController>, TArray<FTerraDyneUndoEntry>> UndoStacks;
	TMap<TWeakObjectPtr<APlayerController>, TArray<FTerraDyneUndoEntry>> RedoStacks;
	TOptional<FTerraDyneUndoEntry> PendingStroke;
	TWeakObjectPtr<APlayerController> PendingStrokeOwner;

	void LoadMaterials();
	void SetupLighting();
	void SpawnDefaultSandboxChunk();
};
