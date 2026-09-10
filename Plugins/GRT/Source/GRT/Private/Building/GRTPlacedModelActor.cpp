// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/GRTPlacedModelActor.h"
#include "Net/UnrealNetwork.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "GRTModule.h"

AGRTPlacedModelActor::AGRTPlacedModelActor()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ImportedModelMaterialFinder(
		TEXT("/GRT/Materials/Models/M_GRT_ImportedModel.M_GRT_ImportedModel"));
	ImportedModelBaseMaterial = ImportedModelMaterialFinder.Object;

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
	// AGRTEditController::TraceUnderCursor, so it cannot be clicked or selected at all.
	// bImmediateUpdate is false here because there is no mesh yet -- SetModelAsset rebuilds it.
	DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, false);
}

void AGRTPlacedModelActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGRTPlacedModelActor, AssetId);
	DOREPLIFETIME(AGRTPlacedModelActor, ModelName);
	DOREPLIFETIME(AGRTPlacedModelActor, SourceFilePath);
	DOREPLIFETIME(AGRTPlacedModelActor, CustomLabel);
	DOREPLIFETIME(AGRTPlacedModelActor, ReplicatedScale3D);
	DOREPLIFETIME(AGRTPlacedModelActor, bIsVisibleInScene);
}

void AGRTPlacedModelActor::BeginPlay()
{
	Super::BeginPlay();

	// A client can finish spawning this actor before either replicated property has landed, and can
	// equally receive both before BeginPlay. Resolving here as well as from both OnReps covers every
	// ordering; ResolveModelAsset no-ops once the geometry is bound.
	ResolveModelAsset();
}

void AGRTPlacedModelActor::SetModelAsset(const FGRTImportedModelAsset& Asset)
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

void AGRTPlacedModelActor::ApplyMaterials(const TArray<FGRTImportedMaterial>& Materials)
{
	if (!DynamicMeshComponent) return;

	// A lit base, never an unlit one: an MSM_Unlit base (EmissiveTexturedMaterial, tried earlier)
	// made every textured model self-illuminate regardless of scene lighting.
	// M_GRT_ImportedModel is lit and declares BaseColor x BaseColorTexture. BasicShapeMaterial is
	// only the fallback -- it declares "Color" and no texture at all, which is why textured models
	// used to render as flat colour.
	UMaterialInterface* BaseMat = ImportedModelBaseMaterial.Get();
	if (!BaseMat)
	{
		UE_LOG(LogGRT, Warning, TEXT("GRT: M_GRT_ImportedModel is missing; imported models fall back to BasicShapeMaterial and show no textures."));
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!BaseMat)
	{
		return;
	}

	for (int32 SlotIdx = 0; SlotIdx < Materials.Num(); ++SlotIdx)
	{
		const FGRTImportedMaterial& MatInfo = Materials[SlotIdx];
		UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (!DMI)
		{
			continue;
		}

		// No EmissiveColor -- an ordinary diffuse colour has no business driving self-illumination.
		DMI->SetVectorParameterValue(TEXT("BaseColor"), MatInfo.DiffuseColor); // M_GRT_ImportedModel
		DMI->SetVectorParameterValue(TEXT("Color"), MatInfo.DiffuseColor);     // BasicShapeMaterial fallback
		if (MatInfo.DiffuseTexture)
		{
			DMI->SetTextureParameterValue(TEXT("BaseColorTexture"), MatInfo.DiffuseTexture);
		}
		DynamicMeshComponent->SetMaterial(SlotIdx, DMI);
	}
}

void AGRTPlacedModelActor::SetSelectionState(bool bSelected)
{
	bIsSelectedInEditor = bSelected;
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->SetRenderCustomDepth(bSelected);
		DynamicMeshComponent->SetCustomDepthStencilValue(bSelected ? 250 : 0);
	}
}

void AGRTPlacedModelActor::SetVisibleInScene(bool bVisible)
{
	bIsVisibleInScene = bVisible;
	SetActorHiddenInGame(!bVisible);
	SetActorEnableCollision(bVisible);
}

void AGRTPlacedModelActor::NotifyScaleChanged()
{
	ReplicatedScale3D = GetActorScale3D();
}

void AGRTPlacedModelActor::OnRep_Scale3D()
{
	SetActorScale3D(ReplicatedScale3D);
}

void AGRTPlacedModelActor::ResolveModelAsset()
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
	UGRTAssetLibrarySubsystem* AssetLib = World ? World->GetSubsystem<UGRTAssetLibrarySubsystem>() : nullptr;
	if (!AssetLib)
	{
		return;
	}

	FString Error;
	if (const FGRTImportedModelAsset* Found = AssetLib->ResolveOrImport(AssetId, SourceFilePath, Error))
	{
		SetModelAsset(*Found);
	}
	else if (!Error.IsEmpty())
	{
		UE_LOG(LogGRT, Warning, TEXT("GRT: placed model '%s' could not resolve its geometry here: %s"),
			*ModelName, *Error);
	}
}

void AGRTPlacedModelActor::OnRep_AssetId()
{
	ResolveModelAsset();
}

void AGRTPlacedModelActor::OnRep_SourceFilePath()
{
	ResolveModelAsset();
}

void AGRTPlacedModelActor::OnRep_ModelName()
{
}

FGRTPlacedModelData AGRTPlacedModelActor::ToData() const
{
	FGRTPlacedModelData Data;
	Data.AssetId = AssetId;
	Data.SourceFilePath = SourceFilePath;
	Data.CustomLabel = CustomLabel;
	Data.WorldTransform = GetActorTransform();
	Data.bVisibleInScene = bIsVisibleInScene;
	return Data;
}

void AGRTPlacedModelActor::ApplyData(const FGRTPlacedModelData& Data)
{
	AssetId = Data.AssetId;
	SourceFilePath = Data.SourceFilePath;
	CustomLabel = Data.CustomLabel;
	SetActorTransform(Data.WorldTransform);
	ReplicatedScale3D = Data.WorldTransform.GetScale3D();

	ResolveModelAsset();
	SetVisibleInScene(Data.bVisibleInScene);
}

void AGRTPlacedModelActor::OnRep_CustomLabel()
{
}

void AGRTPlacedModelActor::OnRep_VisibleInScene()
{
	// bHidden replicates on its own; collision does not, so a hidden model would still block traces
	// and selection on every client without this.
	SetVisibleInScene(bIsVisibleInScene);
}
