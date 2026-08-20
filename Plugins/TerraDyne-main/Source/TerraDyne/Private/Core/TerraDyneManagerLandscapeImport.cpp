// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// ATerraDyneManager -- authored Landscape conversion.
//
// Second translation unit split out of TerraDyneManager.cpp. Same members, same class, same API;
// only the file boundary is new. See TerraDyneManagerPopulation.cpp for the first.
//
// What lives here: turning an authored ALandscapeProxy into TerraDyne chunks -- height
// resampling, paint/weight layer capture, placed-foliage and actor-foliage transfer, and the
// editor entry points that drive it (ManualImport, MigrateLandscapeProject).
//
// The whole file is editor-only. Landscape editing and the foliage types it reads are not
// available in a packaged build, so every symbol below sits behind WITH_EDITOR -- which is also
// why this was the cleanest of the manager's concerns to lift out: the fence already marked the
// boundary, and no landscape or foliage symbol is referenced anywhere else in the manager.
//
// BeginPlay still calls ImportInternal directly for bAutoImportAtRuntime; that call site stays
// with the startup logic in TerraDyneManager.cpp.

#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneManagerInternal.h"
#include "Core/TerraDyneSubsystem.h"
#include "Settings/TerraDyneSettings.h"
#include "World/TerraDyneChunk.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "NavigationSystem.h"

#if WITH_EDITOR
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#include "InstancedFoliageActor.h"
#include "FoliageType_Actor.h"
#include "FoliageType_InstancedStaticMesh.h"
#endif

// Landscape migration still records which paint layers the source landscape had, purely as
// metadata — TerraDyne itself no longer has paint layers to import them into.
static constexpr int32 MaxCapturedLandscapeLayerMappings = 4;

#if WITH_EDITOR
void ATerraDyneManager::ManualImport()
{
	ImportFromLandscape(TargetLandscapeSource, true);
}

void ATerraDyneManager::MigrateLandscapeProject()
{
	ImportFromLandscapeWithOptions(TargetLandscapeSource, LandscapeMigrationOptions);
}

void ATerraDyneManager::ResampleLandscapeData(ATerraDyneChunk* Chunk, ULandscapeComponent* SourceComponent)
{
	if (!Chunk || !SourceComponent)
	{
		return;
	}

	ALandscapeProxy* Proxy = SourceComponent->GetLandscapeProxy();
	ULandscapeInfo* Info = Proxy ? Proxy->GetLandscapeInfo() : nullptr;
	if (!Info)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyne Import: LandscapeInfo unavailable for component %s."), *SourceComponent->GetName());
		return;
	}

	const int32 Quads = SourceComponent->ComponentSizeQuads;
	const int32 Resolution = Quads + 1;
	const int32 NumSamples = Resolution * Resolution;
	if (Resolution < 2 || NumSamples <= 0)
	{
		return;
	}

	const int32 X1 = SourceComponent->SectionBaseX;
	const int32 Y1 = SourceComponent->SectionBaseY;
	const int32 X2 = X1 + Quads;
	const int32 Y2 = Y1 + Quads;

	FLandscapeEditDataInterface EditData(Info);

	TArray<uint16> HeightSamples;
	HeightSamples.SetNumZeroed(NumSamples);
	EditData.GetHeightDataFast(X1, Y1, X2, Y2, HeightSamples.GetData(), 0);

	Chunk->Resolution = Resolution;
	Chunk->BaseBuffer.SetNumZeroed(NumSamples);
	Chunk->SculptBuffer.SetNumZeroed(NumSamples);
	Chunk->DetailBuffer.SetNumZeroed(NumSamples);
	Chunk->HeightBuffer.SetNumZeroed(NumSamples);
	// This path resizes the buffers directly instead of going through Initialize(), so the pad
	// layer has to be resized alongside them or RecomputeHeightFromLayers will find it stale.
	Chunk->PadWeightBuffer.SetNumZeroed(NumSamples);
	Chunk->PadTargetBuffer.SetNumZeroed(NumSamples);
	for (int32 Index = 0; Index < NumSamples; Index++)
	{
		Chunk->BaseBuffer[Index] = static_cast<float>(HeightSamples[Index]) / 65535.0f;
	}
	Chunk->RecomputeHeightFromLayers();

	Chunk->RebuildPhysicsMesh();
}

void ATerraDyneManager::ImportFromLandscape(ALandscapeProxy* TargetLandscape, bool bHideSource)
{
	FTerraDyneLandscapeMigrationOptions Options = LandscapeMigrationOptions;
	Options.bHideSourceLandscape = bHideSource;
	ImportFromLandscapeWithOptions(TargetLandscape, Options);
}

void ATerraDyneManager::ImportFromLandscapeWithOptions(ALandscapeProxy* TargetLandscape, const FTerraDyneLandscapeMigrationOptions& Options)
{
	ImportInternal(TargetLandscape, Options);
}

void ATerraDyneManager::ImportInternal(ALandscapeProxy* Source, const FTerraDyneLandscapeMigrationOptions& Options)
{
	if (!Source || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("TerraDyne Import: Invalid landscape source."));
		return;
	}

	TargetLandscapeSource = Source;

	const bool bPreviousStreamingPaused = bStreamingPaused;
	if (Options.bPauseStreamingDuringImport)
	{
		bStreamingPaused = true;
	}

	PendingLoadQueue.Empty();
	PendingUnloadQueue.Empty();
	DirtyChunkSet.Empty();
	ImportedChunkCoords.Empty();
	LastStreamingHash = 0;
	ClearUndoRedoState();

	if (Options.bClearExistingChunks)
	{
		TArray<AActor*> ExistingChunks;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), ExistingChunks);
		for (AActor* Actor : ExistingChunks)
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}
		ActiveChunkMap.Reset();
	}

	LandscapeMigrationState = FTerraDyneLandscapeMigrationState();
	LandscapeMigrationState.bWasImportedFromLandscape = true;
	WorldOriginKind = ETerraDyneChunkOrigin::ImportedLandscape;
	LandscapeMigrationState.SourceLandscapeName = Source->GetName();
	LandscapeMigrationState.SourceLandscapePath = Source->GetPathName();
	LandscapeMigrationState.SourceLandscapeLocation = Source->GetActorLocation();
	LandscapeMigrationState.SourceLandscapeScale = Source->GetActorScale3D();
	LandscapeMigrationState.SourceLandscapeMaterialPath = Source->LandscapeMaterial ? Source->LandscapeMaterial->GetPathName() : FString();
	LandscapeMigrationState.bImportedPaintLayers = Options.bImportWeightLayers;
	LandscapeMigrationState.bSourceLandscapeHidden = Options.bHideSourceLandscape;
	LandscapeMigrationState.ImportedAtIso8601 = FDateTime::UtcNow().ToIso8601();

	const FVector LandscapeScale = Source->GetActorScale3D();
	if (LandscapeScale.X > KINDA_SMALL_NUMBER)
	{
		GlobalChunkSize = Source->ComponentSizeQuads * LandscapeScale.X;
	}

	const FVector SourceLocation = Source->GetActorLocation();
	SetActorLocation(FVector(
		SourceLocation.X + (GlobalChunkSize * 0.5f),
		SourceLocation.Y + (GlobalChunkSize * 0.5f),
		SourceLocation.Z - (256.0f * LandscapeScale.Z)));
	LandscapeMigrationState.RuntimeManagerLocation = GetActorLocation();
	LandscapeMigrationState.ImportedChunkSize = GlobalChunkSize;

	if (Options.bAdoptLandscapeMaterialAsMasterMaterial && Source->LandscapeMaterial)
	{
		MasterMaterial = Source->LandscapeMaterial;
		LandscapeMigrationState.bAdoptedLandscapeMaterial = true;
	}

	if (Options.bImportWeightLayers || Options.bCaptureLayerMappings)
	{
		TSet<FName> SeenMappedLayers;
		TSet<FName> SeenUnmappedLayers;
		for (ULandscapeComponent* Component : Source->LandscapeComponents)
		{
			if (!Component)
			{
				continue;
			}

			for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations())
			{
				if (!Allocation.LayerInfo)
				{
					continue;
				}

				const FName LayerName = Allocation.LayerInfo->GetLayerName() != NAME_None
					? Allocation.LayerInfo->GetLayerName()
					: Allocation.LayerInfo->GetFName();

				if (SeenMappedLayers.Contains(LayerName) || SeenUnmappedLayers.Contains(LayerName))
				{
					continue;
				}

				if ((Options.bImportWeightLayers || Options.bCaptureLayerMappings) &&
					LandscapeMigrationState.LayerMappings.Num() < MaxCapturedLandscapeLayerMappings)
				{
					FTerraDyneLandscapeLayerMapping Mapping;
					Mapping.SourceLayerName = LayerName;
					Mapping.TerraDyneWeightLayerIndex = LandscapeMigrationState.LayerMappings.Num();
					LandscapeMigrationState.LayerMappings.Add(Mapping);
					SeenMappedLayers.Add(LayerName);
				}
				else
				{
					LandscapeMigrationState.UnmappedLayerNames.Add(LayerName.ToString());
					SeenUnmappedLayers.Add(LayerName);
				}
			}
		}
	}

	LandscapeMigrationState.ImportedWeightLayerCount = LandscapeMigrationState.LayerMappings.Num();
	LandscapeMigrationState.bImportedPaintLayers =
		Options.bImportWeightLayers && LandscapeMigrationState.ImportedWeightLayerCount > 0;
	LandscapeMigrationState.bTransferredPlacedFoliage = false;
	LandscapeMigrationState.bTransferredFoliageFollowsTerrain = false;
	LandscapeMigrationState.ImportedFoliageDefinitionCount = 0;
	LandscapeMigrationState.ImportedFoliageInstanceCount = 0;

	TSet<FString> ImportedFoliageDefinitions;
	int32 ImportedFoliageInstances = 0;

	auto CapturePlacedFoliageForComponent =
		[this, &ImportedFoliageDefinitions, &ImportedFoliageInstances, &Options](ATerraDyneChunk* Chunk, ULandscapeComponent* Component)
	{
		if (!Chunk || !Component)
		{
			return;
		}

		AInstancedFoliageActor* FoliageActor =
			AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Component->GetComponentLevel(), false);
		if (!FoliageActor)
		{
			return;
		}

		const TMap<UFoliageType*, TArray<const FFoliageInstancePlacementInfo*>> FoliageByType =
			FoliageActor->GetInstancesForComponent(Component);
		if (FoliageByType.Num() == 0)
		{
			return;
		}

		TArray<FString> StaticMeshPaths;
		TArray<int32> MaterialCounts;
		TArray<FString> OverrideMaterialPaths;
		TArray<int32> DefinitionIndices;
		TArray<FTransform> LocalTransforms;
		TArray<float> TerrainOffsets;
		TMap<FString, int32> DefinitionLookup;
		TArray<FString> ActorClassPaths;
		TArray<uint8> ActorAttachFlags;
		TArray<int32> ActorDefinitionIndices;
		TArray<FTransform> ActorLocalTransforms;
		TArray<float> ActorTerrainOffsets;
		TMap<FString, int32> ActorDefinitionLookup;

		for (const auto& FoliagePair : FoliageByType)
		{
			const UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliagePair.Key);
			const UFoliageType_Actor* ActorType = Cast<UFoliageType_Actor>(FoliagePair.Key);
			if (StaticMeshType && StaticMeshType->Mesh)
			{
				const FString MeshPath = StaticMeshType->Mesh->GetPathName();
				TArray<FString> MaterialPaths;
				MaterialPaths.Reserve(StaticMeshType->OverrideMaterials.Num());
				for (UMaterialInterface* OverrideMaterial : StaticMeshType->OverrideMaterials)
				{
					if (!OverrideMaterial)
					{
						MaterialPaths.Add(FString());
						continue;
					}

					const FString MaterialPath = OverrideMaterial->GetPathName();
					MaterialPaths.Add(MaterialPath.StartsWith(TEXT("/Engine/Transient")) ? FString() : MaterialPath);
				}

				FString DefinitionKey = MeshPath;
				for (const FString& MaterialPath : MaterialPaths)
				{
					DefinitionKey += TEXT("|");
					DefinitionKey += MaterialPath;
				}

				int32 DefinitionIndex = INDEX_NONE;
				if (const int32* ExistingIndex = DefinitionLookup.Find(DefinitionKey))
				{
					DefinitionIndex = *ExistingIndex;
				}
				else
				{
					DefinitionIndex = StaticMeshPaths.Add(MeshPath);
					DefinitionLookup.Add(DefinitionKey, DefinitionIndex);
					MaterialCounts.Add(MaterialPaths.Num());
					OverrideMaterialPaths.Append(MaterialPaths);
					ImportedFoliageDefinitions.Add(DefinitionKey);
				}

				for (const FFoliageInstancePlacementInfo* PlacementInfo : FoliagePair.Value)
				{
					if (!PlacementInfo)
					{
						continue;
					}

					const FTransform WorldTransform(
						FQuat(PlacementInfo->Rotation),
						PlacementInfo->Location,
						FVector(PlacementInfo->DrawScale3D.X, PlacementInfo->DrawScale3D.Y, PlacementInfo->DrawScale3D.Z));
					const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Chunk->GetActorTransform());

					DefinitionIndices.Add(DefinitionIndex);
					LocalTransforms.Add(LocalTransform);
					TerrainOffsets.Add(
						LocalTransform.GetLocation().Z -
						Chunk->GetHeightAtLocation(FVector(LocalTransform.GetLocation().X, LocalTransform.GetLocation().Y, 0.0f)));
				}
			}
			else if (ActorType && ActorType->ActorClass)
			{
				const FString ActorClassPath = ActorType->ActorClass->GetPathName();
				const FString DefinitionKey = FString::Printf(
					TEXT("Actor:%s|%d"),
					*ActorClassPath,
					ActorType->bShouldAttachToBaseComponent ? 1 : 0);

				int32 DefinitionIndex = INDEX_NONE;
				if (const int32* ExistingIndex = ActorDefinitionLookup.Find(DefinitionKey))
				{
					DefinitionIndex = *ExistingIndex;
				}
				else
				{
					DefinitionIndex = ActorClassPaths.Add(ActorClassPath);
					ActorDefinitionLookup.Add(DefinitionKey, DefinitionIndex);
					ActorAttachFlags.Add(ActorType->bShouldAttachToBaseComponent ? 1 : 0);
					ImportedFoliageDefinitions.Add(DefinitionKey);
				}

				for (const FFoliageInstancePlacementInfo* PlacementInfo : FoliagePair.Value)
				{
					if (!PlacementInfo)
					{
						continue;
					}

					const FTransform WorldTransform(
						FQuat(PlacementInfo->Rotation),
						PlacementInfo->Location,
						FVector(PlacementInfo->DrawScale3D.X, PlacementInfo->DrawScale3D.Y, PlacementInfo->DrawScale3D.Z));
					const FTransform LocalTransform = WorldTransform.GetRelativeTransform(Chunk->GetActorTransform());

					ActorDefinitionIndices.Add(DefinitionIndex);
					ActorLocalTransforms.Add(LocalTransform);
					ActorTerrainOffsets.Add(
						LocalTransform.GetLocation().Z -
						Chunk->GetHeightAtLocation(FVector(LocalTransform.GetLocation().X, LocalTransform.GetLocation().Y, 0.0f)));
				}
			}
		}

		if (LocalTransforms.Num() == 0 && ActorLocalTransforms.Num() == 0)
		{
			return;
		}

		ImportedFoliageInstances += LocalTransforms.Num() + ActorLocalTransforms.Num();
		Chunk->SetTransferredFoliageData(
			StaticMeshPaths,
			MaterialCounts,
			OverrideMaterialPaths,
			DefinitionIndices,
			LocalTransforms,
			TerrainOffsets,
			Options.bTransferredFoliageFollowsTerrain);
		Chunk->SetTransferredActorFoliageData(
			ActorClassPaths,
			ActorAttachFlags,
			ActorDefinitionIndices,
			ActorLocalTransforms,
			ActorTerrainOffsets);
	};

	int32 ImportedChunks = 0;
	int32 ImportedResolution = 0;
	for (ULandscapeComponent* Component : Source->LandscapeComponents)
	{
		if (!Component)
		{
			continue;
		}

		const FIntPoint Coord = Component->GetSectionBase() / Component->ComponentSizeQuads;
		ImportedChunkCoords.Add(Coord);
		ATerraDyneChunk* Chunk = SpawnConfiguredChunk(Coord, Component->ComponentSizeQuads + 1);
		if (!Chunk)
		{
			continue;
		}

		Chunk->ZScale = 512.0f * LandscapeScale.Z;
		ResampleLandscapeData(Chunk, Component);
		if (Options.bTransferPlacedFoliage)
		{
			CapturePlacedFoliageForComponent(Chunk, Component);
		}
		ImportedResolution = FMath::Max(ImportedResolution, Component->ComponentSizeQuads + 1);
		ImportedChunks++;
	}

	LandscapeMigrationState.ImportedComponentCount = ImportedChunks;
	LandscapeMigrationState.ImportedResolution = ImportedResolution;
	LandscapeMigrationState.bTransferredPlacedFoliage =
		Options.bTransferPlacedFoliage && ImportedFoliageInstances > 0;
	LandscapeMigrationState.bTransferredFoliageFollowsTerrain =
		LandscapeMigrationState.bTransferredPlacedFoliage && Options.bTransferredFoliageFollowsTerrain;
	LandscapeMigrationState.ImportedFoliageDefinitionCount = ImportedFoliageDefinitions.Num();
	LandscapeMigrationState.ImportedFoliageInstanceCount = ImportedFoliageInstances;

	RebuildChunkMap();
	ApplyMaterialToActiveChunks();

	if (Options.bHideSourceLandscape)
	{
		Source->SetActorHiddenInGame(true);
		Source->SetIsTemporarilyHiddenInEditor(true);
	}

	bStreamingPaused = bPreviousStreamingPaused;

	UE_LOG(LogTemp, Log, TEXT("TerraDyne Import: Imported %d landscape components from %s."), ImportedChunks, *Source->GetName());
}
#endif
