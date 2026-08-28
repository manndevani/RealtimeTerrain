// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/TerraDynePlacedModelActor.h"
#include "Net/UnrealNetwork.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
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
}

void ATerraDynePlacedModelActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATerraDynePlacedModelActor, AssetId);
	DOREPLIFETIME(ATerraDynePlacedModelActor, ModelName);
	DOREPLIFETIME(ATerraDynePlacedModelActor, CustomLabel);
	DOREPLIFETIME(ATerraDynePlacedModelActor, ReplicatedScale3D);
}

void ATerraDynePlacedModelActor::SetModelAsset(const FTerraDyneImportedModelAsset& Asset)
{
	AssetId = Asset.AssetId;
	ModelName = Asset.AssetName;
	if (CustomLabel.IsEmpty())
	{
		CustomLabel = Asset.AssetName;
	}

	if (DynamicMeshComponent && DynamicMeshComponent->GetDynamicMesh())
	{
		DynamicMeshComponent->GetDynamicMesh()->SetMesh(Asset.Mesh);
		DynamicMeshComponent->NotifyMeshUpdated();
	}

	ApplyMaterials(Asset.Materials);
}

void ATerraDynePlacedModelActor::ApplyMaterials(const TArray<FTerraDyneImportedMaterial>& Materials)
{
	if (!DynamicMeshComponent) return;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* BaseMat = DefaultMatFinder.Object ? DefaultMatFinder.Object.Get() : nullptr;
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	for (int32 SlotIdx = 0; SlotIdx < Materials.Num(); ++SlotIdx)
	{
		const FTerraDyneImportedMaterial& MatInfo = Materials[SlotIdx];
		if (BaseMat)
		{
			UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(BaseMat, this);
			if (DMI)
			{
				DMI->SetVectorParameterValue(TEXT("Color"), MatInfo.DiffuseColor);
				DMI->SetVectorParameterValue(TEXT("BaseColor"), MatInfo.DiffuseColor);
				if (MatInfo.DiffuseTexture)
				{
					DMI->SetTextureParameterValue(TEXT("Texture"), MatInfo.DiffuseTexture);
					DMI->SetTextureParameterValue(TEXT("BaseColorTexture"), MatInfo.DiffuseTexture);
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

void ATerraDynePlacedModelActor::OnRep_AssetId()
{
	if (UWorld* World = GetWorld())
	{
		if (UTerraDyneAssetLibrarySubsystem* AssetLib = World->GetSubsystem<UTerraDyneAssetLibrarySubsystem>())
		{
			if (const FTerraDyneImportedModelAsset* Found = AssetLib->FindAsset(AssetId))
			{
				SetModelAsset(*Found);
			}
		}
	}
}

void ATerraDynePlacedModelActor::OnRep_ModelName()
{
}

void ATerraDynePlacedModelActor::OnRep_CustomLabel()
{
}
