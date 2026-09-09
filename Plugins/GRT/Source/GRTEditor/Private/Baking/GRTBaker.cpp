// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Baking/GRTBaker.h"
#include "World/GRTTileData.h"

// Engine Includes
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

/**
 * UE Landscape stores height as 16-bit values with 128 cm/unit vertical scale.
 * This constant converts component transform scale to the full height range.
 */
static constexpr float LandscapeHeightScale = 128.0f;

TArray<UGRTTileData*> UGRTBaker::BakeLandscapeToAssets(ALandscapeProxy* SourceLandscape, FString DestinationPath)
{
    TArray<UGRTTileData*> CreatedAssets;

    if (!SourceLandscape)
    {
        UE_LOG(LogTemp, Warning, TEXT("GRTBaker: Invalid Source Landscape."));
        return CreatedAssets;
    }

    UE_LOG(LogTemp, Log, TEXT("GRTBaker: Starting massive bake for %s..."), *SourceLandscape->GetName());

    // Iterate over every landscape component (Tile)
    for (ULandscapeComponent* Comp : SourceLandscape->LandscapeComponents)
    {
        if (Comp)
        {
            // Auto-generate name based on grid coordinates (e.g. "TD_Tile_0_0")
            // SectionBaseX/Y gives the absolute coordinates in quads
            FString DefaultName = FString::Printf(TEXT("TD_Tile_%d_%d"), Comp->SectionBaseX, Comp->SectionBaseY);

            UGRTTileData* NewAsset = BakeComponent(Comp, DestinationPath, DefaultName);
            
            if (NewAsset)
            {
                CreatedAssets.Add(NewAsset);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GRTBaker: Bake Complete. Created %d Assets."), CreatedAssets.Num());
    return CreatedAssets;
}

UGRTTileData* UGRTBaker::BakeComponent(ULandscapeComponent* Component, FString DestinationPath, FString AssetNameOverride)
{
    if (!Component) return nullptr;

    // 1. Determine Asset Name and Path
    FString AssetName = AssetNameOverride;
    if (AssetName.IsEmpty())
    {
        AssetName = FString::Printf(TEXT("TD_Tile_%d_%d"), Component->SectionBaseX, Component->SectionBaseY);
    }
    
    // Ensure path ends with /
    if (!DestinationPath.EndsWith(TEXT("/")))
    {
        DestinationPath += TEXT("/");
    }

    FString PackageName = DestinationPath + AssetName;

    // 2. Create the Asset Package
    UPackage* Package = CreatePackage(*PackageName);
    Package->FullyLoad();

    // Create the UObject
    UGRTTileData* NewData = NewObject<UGRTTileData>(Package, *AssetName, RF_Public | RF_Standalone);

    // 3. Extract Metadata
    float QuadCount = Component->ComponentSizeQuads; // e.g., 63
    float ScaleX = Component->GetComponentTransform().GetScale3D().X;
    float ScaleZ = Component->GetComponentTransform().GetScale3D().Z;

    NewData->RealWorldSize = QuadCount * ScaleX;
    NewData->BakedZScale = ScaleZ * LandscapeHeightScale;
    // Store grid coordinates for debugging
    NewData->GridCoordinate = FIntPoint(Component->SectionBaseX, Component->SectionBaseY);
    NewData->SourceComponentName = Component->GetName();

    // 4. Extract Heavy Data
    int32 HeightRes = 0;
    if (ExtractHeightmapData(Component, NewData->InitialHeightMap, HeightRes))
    {
        NewData->Resolution = HeightRes;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GRTBaker: Failed to extract Heightmap for %s"), *AssetName);
    }

    int32 WeightRes = 0;


    // 5. Save and Register
    NewData->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(NewData);

    // Optional: Auto-Save the asset to disk immediately
    /*
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, NewData, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);
    */

    return NewData;
}

bool UGRTBaker::ExtractHeightmapData(ULandscapeComponent* Comp, TArray<uint16>& OutData, int32& OutRes)
{
    UTexture2D* HeightTex = Comp->GetHeightmap();
    if (!HeightTex) return false;

    // In Editor, we can access the Source Data or Platform Data.
    // PlatformData gives us what is effectively compiled for the platform.
    
    // 1. Prepare to read Mips
    // Note: This requires the texture to be CPU accessible or Source info available.
    // For standard Baking, Source is preferred, but here we show PlatformData access 
    // which works on loaded textures.
    FTexturePlatformData* PlatformData = HeightTex->GetPlatformData();
    if (!PlatformData || PlatformData->Mips.Num() == 0) return false;

    int32 Width = PlatformData->Mips[0].SizeX;
    int32 Height = PlatformData->Mips[0].SizeY;
    OutRes = Width;

    OutData.Reset();
    OutData.SetNumUninitialized(Width * Height);

    // 2. Lock Buffer
    // Landscape Heightmaps are typically BGRA8 or R8G8.
    // Standard UE Landscape separates 16-bit height into R and G channels.
    const void* RawData = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_ONLY);
    
    if (RawData)
    {
        // Assuming BGRA format (Typical)
        const FColor* Pixels = static_cast<const FColor*>(RawData);

        for (int32 i = 0; i < OutData.Num(); i++)
        {
            const FColor& P = Pixels[i];
            
            // Decode UE Landscape Height
            // Usually: ((R << 8) | G) - 32768 for World Z.
            // But we want raw local 0-65535 storage.
            // Note: Channel order depends on platform. Usually R=High, G=Low.
            
            uint16 HeightValue = (P.R << 8) | P.G;
            OutData[i] = HeightValue;
        }
    }
    else
    {
        PlatformData->Mips[0].BulkData.Unlock();
        return false;
    }

    PlatformData->Mips[0].BulkData.Unlock();
    return true;
}

bool UGRTBaker::ExtractWeightmapData(ULandscapeComponent* Comp, TArray<FColor>& OutData, int32& OutRes)
{
    // Use FLandscapeEditDataInterface — the public editor API for reading landscape weight data.
    // This avoids private WeightmapTextures access restrictions in UE 5.6.
    ALandscapeProxy* Proxy = Comp->GetLandscapeProxy();
    if (!Proxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("GRTBaker: Component has no LandscapeProxy."));
        return false;
    }

    ULandscapeInfo* Info = Proxy->GetLandscapeInfo();
    if (!Info)
    {
        UE_LOG(LogTemp, Warning, TEXT("GRTBaker: LandscapeInfo not available. Ensure landscape is registered."));
        return false;
    }

    // Collect paint layer infos (up to 4 for RGBA channels)
    TArray<ULandscapeLayerInfoObject*> LayerInfos;
    for (const FWeightmapLayerAllocationInfo& Alloc : Comp->GetWeightmapLayerAllocations())
    {
        if (Alloc.LayerInfo && !LayerInfos.Contains(Alloc.LayerInfo))
        {
            LayerInfos.Add(Alloc.LayerInfo);
            if (LayerInfos.Num() >= 4) break;
        }
    }

    if (LayerInfos.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("GRTBaker: No paint layers found on component — skipping weight extraction."));
        return false;
    }

    int32 Quads = Comp->ComponentSizeQuads;
    int32 Size = Quads + 1; // Vertex count = quads + 1
    OutRes = Size;
    OutData.SetNumZeroed(Size * Size);

    // Component bounds in landscape space
    int32 X1 = Comp->SectionBaseX;
    int32 Y1 = Comp->SectionBaseY;
    int32 X2 = X1 + Quads;
    int32 Y2 = Y1 + Quads;

    FLandscapeEditDataInterface EditData(Info);

    for (int32 LayerIdx = 0; LayerIdx < LayerInfos.Num(); LayerIdx++)
    {
        TArray<uint8> WeightData;
        int32 QX1 = X1, QY1 = Y1, QX2 = X2, QY2 = Y2;
        EditData.GetWeightDataFast(LayerInfos[LayerIdx], QX1, QY1, QX2, QY2, &WeightData, 0);

        int32 Stride = QX2 - QX1 + 1;
        for (int32 Y = 0; Y < Size && Y < (QY2 - QY1 + 1); Y++)
        {
            for (int32 X = 0; X < Size && X < Stride; X++)
            {
                int32 SrcIdx = Y * Stride + X;
                int32 DstIdx = Y * Size + X;
                if (!WeightData.IsValidIndex(SrcIdx) || !OutData.IsValidIndex(DstIdx)) continue;

                uint8 Val = WeightData[SrcIdx];
                switch (LayerIdx)
                {
                case 0: OutData[DstIdx].R = Val; break;
                case 1: OutData[DstIdx].G = Val; break;
                case 2: OutData[DstIdx].B = Val; break;
                case 3: OutData[DstIdx].A = Val; break;
                }
            }
        }

        UE_LOG(LogTemp, Log, TEXT("GRTBaker: Extracted weight layer %d (%s) — %d pixels"),
            LayerIdx, *LayerInfos[LayerIdx]->LayerName.ToString(), WeightData.Num());
    }

    return true;
}
