// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWorld;
namespace UE { namespace Geometry { class FDynamicMesh3; } }

/**
 * Runtime thumbnail rendering for imported 3D models.
 *
 * The asset palette originally showed an image only when a model happened to ship a companion PNG
 * beside it; everything else fell back to a bare "3D" placeholder. This renders the real geometry
 * instead, and caches the result to disk so re-importing the same file never pays for it twice.
 *
 * Requires a real RHI. Every entry point checks FApp::CanEverRender() and returns null under
 * -nullrhi (headless automation, dedicated servers) rather than handing back a black texture.
 * Callers treat null as "no thumbnail" and keep the placeholder, so this can only ever add a
 * picture -- it can never break the palette.
 */
class GRT_API FGRTModelThumbnail
{
public:
	/**
	 * Returns a thumbnail for Mesh, reusing the on-disk cache for SourceFilePath when one exists and
	 * rendering (then caching) otherwise. Null when rendering is unavailable or the mesh is empty.
	 *
	 * World is only borrowed: the capture actors are spawned RF_Transient, parked far away from
	 * anything else, and destroyed before this returns.
	 */
	static UTexture2D* GetOrRender(UWorld* World, const FString& SourceFilePath, const UE::Geometry::FDynamicMesh3& Mesh, int32 Resolution = 128);

	/** Absolute path this source file's cached thumbnail occupies under Saved/. */
	static FString GetCachePathForSource(const FString& SourceFilePath);

	/** Loads a previously cached thumbnail, or null if none has been written. */
	static UTexture2D* LoadCached(const FString& SourceFilePath);
};
