// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/TerraDynePlacedModelActor.h"
#include "Net/UnrealNetwork.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "TerraDyneModule.h"

ATerraDynePlacedModelActor::ATerraDynePlacedModelActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
	DynamicMeshComponent->SetupAttachment(SceneRoot);
	DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DynamicMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	DynamicMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	DynamicMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DynamicMeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	DynamicMeshComponent->SetGenerateOverlapEvents(true);

	// Collision responses alone give a UDynamicMeshComponent nothing to hit: it only ever reports
	// physics geometry when bEnableComplexCollision is set (ContainsPhysicsTriMeshData gates on it
	// outright), and that flag defaults to false. Without this the placed model is invisible to
	// ATerraDyneEditController::TraceUnderCursor, so it cannot be clicked or selected at all.
	// bImmediateUpdate is false here because there is no mesh yet -- SetModelAsset rebuilds it.
	DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, false);
}

void ATerraDynePlacedModelActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATerraDynePlacedModelActor, AssetId);
	DOREPLIFETIME(ATerraDynePlacedModelActor, ModelName);
	DOREPLIFETIME(ATerraDynePlacedModelActor, SourceFilePath);
	DOREPLIFETIME(ATerraDynePlacedModelActor, CustomLabel);
	DOREPLIFETIME(ATerraDynePlacedModelActor, ReplicatedScale3D);
}

void ATerraDynePlacedModelActor::BeginPlay()
{
	Super::BeginPlay();

	// A client can finish spawning this actor before either replicated property has landed, and can
	// equally receive both before BeginPlay. Resolving here as well as from both OnReps covers every
	// ordering; ResolveModelAsset no-ops once the geometry is bound.
	ResolveModelAsset();
}

void ATerraDynePlacedModelActor::SetModelAsset(const FTerraDyneImportedModelAsset& Asset)
{
	AssetId = Asset.AssetId;
	ModelName = Asset.AssetName;
	SourceFilePath = Asset.SourceFilePath;
	if (CustomLabel.IsEmpty())
	{
		CustomLabel = Asset.AssetName;
	}

	if (DynamicMeshComponent && DynamicMeshComponent->GetDynamicMesh())
	{
		DynamicMeshComponent->GetDynamicMesh()->SetMesh(Asset.Mesh);
		DynamicMeshComponent->NotifyMeshUpdated();

		// NotifyMeshUpdated only resets the render proxy -- it never touches physics. The collision
		// body has to be rebuilt explicitly against the new geometry, or the component keeps whatever
		// (empty) body it had and traces still pass straight through.
		DynamicMeshComponent->UpdateCollision(false);
	}

	ApplyMaterials(Asset.Materials);
}

void ATerraDynePlacedModelActor::ApplyMaterials(const TArray<FTerraDyneImportedMaterial>& Materials)
{
	if (!DynamicMeshComponent) return;

	// A single lit base for every slot, textured or not. EmissiveTexturedMaterial (tried here
	// previously) is MSM_Unlit -- confirmed by inspecting the shipped asset -- so routing a
	// model's ordinary diffuse texture through it, plus unconditionally writing EmissiveColor
	// below, made every textured imported model self-illuminate: fully bright regardless of scene
	// lighting, casting no shaded response to a directional light, "glowing" in shadow or at
	// night. BasicShapeMaterial is the one base this function already used, successfully, before
	// a textured variant was added -- reusing it for both cases removes the glow with no new,
	// unverified engine asset in the mix.
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	for (int32 SlotIdx = 0; SlotIdx < Materials.Num(); ++SlotIdx)
	{
		const FTerraDyneImportedMaterial& MatInfo = Materials[SlotIdx];
		if (BaseMat)
		{
			UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(BaseMat, this);
			if (DMI)
			{
				// Parameter names tried on spec: SetVectorParameterValue/SetTextureParameterValue
				// are safe no-ops on a name the material doesn't declare, so trying several common
				// conventions costs nothing when they don't match and helps if a future base
				// material does declare one of them. Deliberately no EmissiveColor -- an ordinary
				// diffuse/base color has no business driving self-illumination.
				DMI->SetVectorParameterValue(TEXT("Color"), MatInfo.DiffuseColor);
				DMI->SetVectorParameterValue(TEXT("BaseColor"), MatInfo.DiffuseColor);
				DMI->SetVectorParameterValue(TEXT("DiffuseColor"), MatInfo.DiffuseColor);

				if (MatInfo.DiffuseTexture)
				{
					DMI->SetTextureParameterValue(TEXT("Texture"), MatInfo.DiffuseTexture);
					DMI->SetTextureParameterValue(TEXT("BaseColorTexture"), MatInfo.DiffuseTexture);
					DMI->SetTextureParameterValue(TEXT("Diffuse"), MatInfo.DiffuseTexture);
					DMI->SetTextureParameterValue(TEXT("DiffuseTexture"), MatInfo.DiffuseTexture);
					DMI->SetTextureParameterValue(TEXT("Albedo"), MatInfo.DiffuseTexture);
				}
				DynamicMeshComponent->SetMaterial(SlotIdx, DMI);
			}
		}
	}
}

void ATerraDynePlacedModelActor::SetSelectionState(bool bSelected)
{
	bIsSelectedInEditor = bSelected;
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->SetRenderCustomDepth(bSelected);
		DynamicMeshComponent->SetCustomDepthStencilValue(bSelected ? 250 : 0);
	}
}

void ATerraDynePlacedModelActor::SetVisibleInScene(bool bVisible)
{
	bIsVisibleInScene = bVisible;
	SetActorHiddenInGame(!bVisible);
	SetActorEnableCollision(bVisible);
}

void ATerraDynePlacedModelActor::NotifyScaleChanged()
{
	ReplicatedScale3D = GetActorScale3D();
}

void ATerraDynePlacedModelActor::OnRep_Scale3D()
{
	SetActorScale3D(ReplicatedScale3D);
}

void ATerraDynePlacedModelActor::ResolveModelAsset()
{
	if (!AssetId.IsValid() || !DynamicMeshComponent)
	{
		return;
	}

	// Already bound -- nothing to do. This is what makes the call safe from BeginPlay and both
	// OnReps without re-uploading geometry each time another property lands.
	if (DynamicMeshComponent->GetMesh() && DynamicMeshComponent->GetMesh()->TriangleCount() > 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	UTerraDyneAssetLibrarySubsystem* AssetLib = World ? World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>() : nullptr;
	if (!AssetLib)
	{
		return;
	}

	FString Error;
	if (const FTerraDyneImportedModelAsset* Found = AssetLib->ResolveOrImport(AssetId, SourceFilePath, Error))
	{
		SetModelAsset(*Found);
	}
	else if (!Error.IsEmpty())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne: placed model '%s' could not resolve its geometry here: %s"),
			*ModelName, *Error);
	}
}

void ATerraDynePlacedModelActor::OnRep_AssetId()
{
	ResolveModelAsset();
}

void ATerraDynePlacedModelActor::OnRep_SourceFilePath()
{
	ResolveModelAsset();
}

void ATerraDynePlacedModelActor::OnRep_ModelName()
{
}

FTerraDynePlacedModelData ATerraDynePlacedModelActor::ToData() const
{
	FTerraDynePlacedModelData Data;
	Data.AssetId = AssetId;
	Data.SourceFilePath = SourceFilePath;
	Data.CustomLabel = CustomLabel;
	Data.WorldTransform = GetActorTransform();
	Data.bVisibleInScene = bIsVisibleInScene;
	return Data;
}

void ATerraDynePlacedModelActor::ApplyData(const FTerraDynePlacedModelData& Data)
{
	AssetId = Data.AssetId;
	SourceFilePath = Data.SourceFilePath;
	CustomLabel = Data.CustomLabel;
	SetActorTransform(Data.WorldTransform);
	ReplicatedScale3D = Data.WorldTransform.GetScale3D();

	ResolveModelAsset();
	SetVisibleInScene(Data.bVisibleInScene);
}

void ATerraDynePlacedModelActor::OnRep_CustomLabel()
{
}
