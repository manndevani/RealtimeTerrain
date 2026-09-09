// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/GRTModelThumbnail.h"
#include "IO/GRTModelImporter.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "RenderingThread.h"
#include "GRTModule.h"
#include "UObject/Package.h"

using UE::Geometry::FDynamicMesh3;

namespace
{
	/** Somewhere nothing else lives, so the capture can only ever see the model it was given. */
	constexpr double ThumbnailStageOffset = 5000000.0;
}

FString FGRTModelThumbnail::GetCachePathForSource(const FString& SourceFilePath)
{
	// Hash the full path rather than using the base filename: two different "chair.obj" files in
	// different folders must not collide on one cache entry.
	const FString FullPath = FPaths::ConvertRelativePathToFull(SourceFilePath);
	const FString Hash = FMD5::HashAnsiString(*FullPath.ToLower());
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GRT"), TEXT("ModelThumbnails"), Hash + TEXT(".png"));
}

UTexture2D* FGRTModelThumbnail::LoadCached(const FString& SourceFilePath)
{
	const FString CachePath = GetCachePathForSource(SourceFilePath);
	if (!IFileManager::Get().FileExists(*CachePath))
	{
		return nullptr;
	}
	return FGRTModelImporter::LoadTextureFromFile(CachePath);
}

UTexture2D* FGRTModelThumbnail::GetOrRender(UWorld* World, const FString& SourceFilePath, const FDynamicMesh3& Mesh, int32 Resolution)
{
	if (UTexture2D* Cached = LoadCached(SourceFilePath))
	{
		return Cached;
	}

	// No RHI means no capture. Bail before touching the render target rather than producing a black
	// square that would look like a broken thumbnail forever after, cached.
	if (!FApp::CanEverRender() || !World || Mesh.TriangleCount() == 0)
	{
		return nullptr;
	}

	const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh.GetBounds(true);
	const double Radius = FMath::Max(Bounds.DiagonalLength() * 0.5, 1.0);
	const FVector Centre = FVector(Bounds.Center()) + FVector(ThumbnailStageOffset, ThumbnailStageOffset, ThumbnailStageOffset);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	AActor* MeshActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(FVector(ThumbnailStageOffset, ThumbnailStageOffset, ThumbnailStageOffset)), SpawnParams);
	AActor* CaptureActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!MeshActor || !CaptureActor)
	{
		if (MeshActor) MeshActor->Destroy();
		if (CaptureActor) CaptureActor->Destroy();
		return nullptr;
	}

	UTexture2D* Result = nullptr;
	{
		UDynamicMeshComponent* MeshComp = NewObject<UDynamicMeshComponent>(MeshActor);
		MeshComp->SetupAttachment(MeshActor->GetRootComponent());
		MeshComp->RegisterComponent();
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->GetDynamicMesh()->SetMesh(Mesh);
		MeshComp->NotifyMeshUpdated();

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		RenderTarget->RenderTargetFormat = RTF_RGBA8;
		RenderTarget->ClearColor = FLinearColor(0.05f, 0.06f, 0.08f, 1.0f);
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->InitAutoFormat(Resolution, Resolution);
		RenderTarget->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
		Capture->SetupAttachment(CaptureActor->GetRootComponent());
		Capture->RegisterComponent();
		Capture->TextureTarget = RenderTarget;
		// BaseColor rather than final colour: the model is staged in empty space with no lights of
		// its own, and adding one would briefly light the player's actual scene. BaseColor reads the
		// GBuffer albedo directly, so the thumbnail is well-lit by construction and identical every
		// run -- flat, but a 42px palette tile does not need shading.
		Capture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->ShowOnlyActors.Add(MeshActor);
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->FOVAngle = 45.0f;

		// Pull back far enough for the bounding sphere to fit the vertical FOV, with a little margin,
		// looking down a three-quarter diagonal so depth reads at thumbnail size.
		const double Distance = (Radius / FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * 0.5f))) * 1.6;
		const FVector ViewDir = FVector(-1.0, -1.0, -0.55).GetSafeNormal();
		Capture->SetWorldLocationAndRotation(Centre - ViewDir * Distance, ViewDir.Rotation());

		Capture->CaptureScene();
		FlushRenderingCommands();

		FImage Image;
		if (FImageUtils::GetRenderTargetImage(RenderTarget, Image))
		{
			Result = FImageUtils::CreateTexture2DFromImage(Image);

			TArray64<uint8> PngData;
			if (FImageUtils::CompressImage(PngData, TEXT("png"), Image, 0))
			{
				const FString CachePath = GetCachePathForSource(SourceFilePath);
				if (!FFileHelper::SaveArrayToFile(PngData, *CachePath))
				{
					UE_LOG(LogGRT, Verbose, TEXT("GRT: could not cache model thumbnail to '%s'."), *CachePath);
				}
			}
		}
	}

	MeshActor->Destroy();
	CaptureActor->Destroy();

	return Result;
}
