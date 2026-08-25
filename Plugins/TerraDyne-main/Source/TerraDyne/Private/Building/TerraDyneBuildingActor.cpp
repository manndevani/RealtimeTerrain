// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Building/TerraDyneBuildingActor.h"
#include "Building/TerraDyneBuildingKit.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "Components/SplineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TerraDyneModule.h"

namespace
{
	/** Wall lengths are authored on a 400 uu grid, so a 1 uu slack is well clear of float noise. */
	constexpr float PlacementTolerance = 1.0f;

	/** Default footprint: 12 m x 8 m, both exact multiples of the 400 uu module. */
	constexpr float DefaultFootprintX = 1200.0f;
	constexpr float DefaultFootprintY = 800.0f;
}

ATerraDyneBuildingActor::ATerraDyneBuildingActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FootprintSpline = CreateDefaultSubobject<USplineComponent>(TEXT("FootprintSpline"));
	FootprintSpline->SetupAttachment(SceneRoot);
	FootprintSpline->SetMobility(EComponentMobility::Movable);

	// A stock USplineComponent starts as a 2-point line, which is not a footprint. Seed the
	// default rectangle here so a freshly placed building generates something immediately.
	const float HX = DefaultFootprintX * 0.5f;
	const float HY = DefaultFootprintY * 0.5f;
	FootprintSpline->ClearSplinePoints(false);
	FootprintSpline->AddSplinePoint(FVector(-HX, -HY, 0.0f), ESplineCoordinateSpace::Local, false);
	FootprintSpline->AddSplinePoint(FVector( HX, -HY, 0.0f), ESplineCoordinateSpace::Local, false);
	FootprintSpline->AddSplinePoint(FVector( HX,  HY, 0.0f), ESplineCoordinateSpace::Local, false);
	FootprintSpline->AddSplinePoint(FVector(-HX,  HY, 0.0f), ESplineCoordinateSpace::Local, false);
	FootprintSpline->SetClosedLoop(true, false);
	FootprintSpline->UpdateSpline();

	BuildingId = FGuid::NewGuid();
	SyncSplinePointsToReplication();
}

void ATerraDyneBuildingActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATerraDyneBuildingActor, FloorCount);
	DOREPLIFETIME(ATerraDyneBuildingActor, Kit);
	DOREPLIFETIME(ATerraDyneBuildingActor, bGenerateWalls);
	DOREPLIFETIME(ATerraDyneBuildingActor, bGenerateFloors);
	DOREPLIFETIME(ATerraDyneBuildingActor, bGenerateRoof);
	DOREPLIFETIME(ATerraDyneBuildingActor, bGenerateCornerPosts);
	DOREPLIFETIME(ATerraDyneBuildingActor, DoorSegmentIndex);
	DOREPLIFETIME(ATerraDyneBuildingActor, BuildingId);
	DOREPLIFETIME(ATerraDyneBuildingActor, ReplicatedSplinePoints);
	DOREPLIFETIME(ATerraDyneBuildingActor, bFlattenTerrain);
	DOREPLIFETIME(ATerraDyneBuildingActor, bAutoPadHeight);
	DOREPLIFETIME(ATerraDyneBuildingActor, PadTargetWorldZ);
	DOREPLIFETIME(ATerraDyneBuildingActor, PadFalloff);
}

void ATerraDyneBuildingActor::SyncSplinePointsToReplication()
{
	ReplicatedSplinePoints.Reset();
	if (FootprintSpline)
	{
		const int32 NumPoints = FootprintSpline->GetNumberOfSplinePoints();
		ReplicatedSplinePoints.Reserve(NumPoints);
		for (int32 i = 0; i < NumPoints; i++)
		{
			ReplicatedSplinePoints.Add(FootprintSpline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local));
		}
	}
}

void ATerraDyneBuildingActor::OnRep_FloorCount()
{
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::OnRep_Kit()
{
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::OnRep_BuildingConfig()
{
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::OnRep_PadSettings()
{
	if (PadTargetWorldZ != 0.0f)
	{
		const FVector Location = GetActorLocation();
		SetActorLocation(FVector(Location.X, Location.Y, PadTargetWorldZ));
	}
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::OnRep_SplinePoints()
{
	if (FootprintSpline && ReplicatedSplinePoints.Num() >= 3)
	{
		FootprintSpline->ClearSplinePoints(false);
		for (const FVector& Pt : ReplicatedSplinePoints)
		{
			FootprintSpline->AddSplinePoint(Pt, ESplineCoordinateSpace::Local, false);
		}
		FootprintSpline->SetClosedLoop(true, false);
		FootprintSpline->UpdateSpline();
		EnforceLinearTangents();
	}
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::BeginPlay()
{
	Super::BeginPlay();
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregistering recomputes the ground this building was standing on, so its pad does not
	// outlive it. Only meaningful while the world is still up — on a world teardown the chunks are
	// going away too.
	if (EndPlayReason != EEndPlayReason::EndPlayInEditor &&
		EndPlayReason != EEndPlayReason::Quit &&
		EndPlayReason != EEndPlayReason::LevelTransition)
	{
		if (ATerraDyneManager* Manager = FindManager())
		{
			Manager->UnregisterBuilding(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ATerraDyneBuildingActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RegenerateBuilding();
	NotifyBuildingChanged();
}

#if WITH_EDITOR
void ATerraDyneBuildingActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Dragging in the editor fires this every frame. Regenerating is cheap (instance transforms
	// only), but a pad refresh walks the distance field over every touched chunk, so that waits
	// until the drag ends.
	RegenerateBuilding();
	if (bFinished)
	{
		NotifyBuildingChanged();
	}
}
#endif

ATerraDyneManager* ATerraDyneBuildingActor::FindManager() const
{
	UWorld* World = GetWorld();
	UTerraDyneSubsystem* Subsystem = World ? World->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Subsystem ? Subsystem->GetTerrainManager() : nullptr;
	if (!Manager && World)
	{
		Manager = Cast<ATerraDyneManager>(UGameplayStatics::GetActorOfClass(World, ATerraDyneManager::StaticClass()));
	}
	return Manager;
}

void ATerraDyneBuildingActor::PostNetReceiveLocationAndRotation()
{
	Super::PostNetReceiveLocationAndRotation();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::RelevelToTerrain()
{
	bPadTargetCached = false;
	NotifyBuildingChanged();
}

float ATerraDyneBuildingActor::ResolvePadTargetWorldZ() const
{
	if (!HasAuthority())
	{
		return PadTargetWorldZ != 0.0f ? PadTargetWorldZ : GetActorLocation().Z;
	}

	if (!bAutoPadHeight)
	{
		return PadTargetWorldZ;
	}

	if (bPadTargetCached)
	{
		return CachedPadTargetWorldZ;
	}

	const ATerraDyneManager* Manager = FindManager();
	const TArray<FVector2D> Polygon = GetFootprintPolygonWorld();
	if (!Manager || Polygon.Num() == 0)
	{
		return GetActorLocation().Z;
	}

	// Sample center, corners, and edge midpoints to accurately capture mountain peaks, ridges, and slopes
	FVector2D Center(0.0, 0.0);
	for (const FVector2D& P : Polygon)
	{
		Center += P;
	}
	Center /= FMath::Max(1, Polygon.Num());

	TArray<FVector2D> SamplePoints = Polygon;
	SamplePoints.Add(Center);

	const int32 NumVerts = Polygon.Num();
	for (int32 i = 0; i < NumVerts; ++i)
	{
		const FVector2D& P0 = Polygon[i];
		const FVector2D& P1 = Polygon[(i + 1) % NumVerts];
		SamplePoints.Add((P0 + P1) * 0.5f);
	}

	double Sum = 0.0;
	int32 Samples = 0;
	float MaxGroundZ = -MAX_FLT;
	for (const FVector2D& P : SamplePoints)
	{
		float GroundZ = 0.0f;
		bool bSampled = Manager->GetNaturalTerrainHeightAtXY(P, GroundZ);
		if (!bSampled)
		{
			// No TerraDyne chunk streamed at this XY -- e.g. the footprint sits over a locally
			// imported Cesium tile instead of TerraDyne's own terrain. Fall back to a raycast against
			// whatever collidable ground is actually there.
			bSampled = TraceGroundHeightAtXY(P, GroundZ);
		}
		if (bSampled)
		{
			MaxGroundZ = FMath::Max(MaxGroundZ, GroundZ);
			Sum += GroundZ;
			++Samples;
		}
	}

	// Neither the chunk buffer nor a raycast found ground under any sample -- no TerraDyne terrain
	// streamed in and nothing collidable underneath either (mid-air, or Cesium tiles not loaded yet).
	if (Samples == 0)
	{
		return GetActorLocation().Z;
	}

	// Use the peak height within the footprint so the building sits cleanly on top of
	// mountain peaks and slopes without any terrain poking through the floor.
	CachedPadTargetWorldZ = (MaxGroundZ > -MAX_FLT) ? MaxGroundZ : static_cast<float>(Sum / Samples);
	bPadTargetCached = true;
	const_cast<ATerraDyneBuildingActor*>(this)->PadTargetWorldZ = CachedPadTargetWorldZ;
	return CachedPadTargetWorldZ;
}

bool ATerraDyneBuildingActor::TraceGroundHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start(WorldXY.X, WorldXY.Y, 20000000.0); // 200 km
	const FVector End(WorldXY.X, WorldXY.Y, -20000000.0);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildingPadGroundSample), false, this);
	Params.AddIgnoredActor(this);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		OutHeightZ = static_cast<float>(Hit.Location.Z);
		return true;
	}

	return false;
}

void ATerraDyneBuildingActor::SnapToPadHeight()
{
	bPadTargetCached = false;
	const float TargetZ = ResolvePadTargetWorldZ();
	const FVector Location = GetActorLocation();
	SetActorLocation(FVector(Location.X, Location.Y, TargetZ));
	PadTargetWorldZ = TargetZ;
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::SetIsHolographicPreview(bool bHolographic)
{
	bIsHolographicPreview = bHolographic;
	if (bIsHolographicPreview)
	{
		bFlattenTerrain = false;
		SetActorEnableCollision(false);
		SetReplicates(false);
		SetCanBeDamaged(false);
		if (ATerraDyneManager* Manager = FindManager())
		{
			Manager->UnregisterBuilding(this);
		}
	}
	ApplyHolographicVisuals();
}

void ATerraDyneBuildingActor::ApplyHolographicVisuals()
{
	if (!bIsHolographicPreview)
	{
		return;
	}

	UMaterialInterface* HoloMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/SD_Art/Industrial_Architecture/Materials/MI_Window_Glass.MI_Window_Glass"));
	if (!HoloMat)
	{
		HoloMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	for (UInstancedStaticMeshComponent* Inst : Instancers)
	{
		if (Inst)
		{
			Inst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Inst->SetCastShadow(false);
			if (HoloMat)
			{
				for (int32 i = 0; i < Inst->GetNumMaterials(); ++i)
				{
					Inst->SetMaterial(i, HoloMat);
				}
			}
		}
	}
}

void ATerraDyneBuildingActor::NotifyBuildingChanged()
{
	if (bIsHolographicPreview)
	{
		return;
	}

	ATerraDyneManager* Manager = FindManager();
	if (!Manager)
	{
		return;
	}

	Manager->RegisterBuilding(this);

	// A real change to the building re-levels it; streaming does not come through here, which is
	// what keeps the pad height stable as chunks load and unload underneath it.
	bPadTargetCached = false;

	const FBox2D CurrentBounds = GetFootprintBoundsWorld(PadFalloff);

	// Union of where it was and where it is, so the chunks it just left get their natural ground
	// back in the same pass that stamps the chunks it just arrived on.
	FBox2D Area = CurrentBounds;
	if (LastPadBoundsWorld.bIsValid)
	{
		Area = CurrentBounds.bIsValid ? (CurrentBounds + LastPadBoundsWorld) : LastPadBoundsWorld;
	}

	LastPadBoundsWorld = CurrentBounds;

	if (Area.bIsValid)
	{
		Manager->RefreshBuildingPads(Area);
	}
}

const UTerraDyneBuildingKit* ATerraDyneBuildingActor::ResolveKit() const
{
	// The kit class defaults describe the SD_Art Industrial set, so an unconfigured building still
	// generates. Reading the CDO rather than instantiating one keeps every unconfigured building
	// from carrying its own private kit subobject into the save.
	return Kit ? Kit : GetDefault<UTerraDyneBuildingKit>();
}

//--------------------------------------------------------------------------------------------------
// Footprint
//--------------------------------------------------------------------------------------------------

void ATerraDyneBuildingActor::ResetToDefaultFootprint()
{
	if (!FootprintSpline)
	{
		return;
	}

	const float HX = DefaultFootprintX * 0.5f;
	const float HY = DefaultFootprintY * 0.5f;

	// Wound counter-clockwise in XY so the polygon test and any future inward offset agree on sign.
	TArray<FVector> Points = {
		FVector(-HX, -HY, 0.0f),
		FVector( HX, -HY, 0.0f),
		FVector( HX,  HY, 0.0f),
		FVector(-HX,  HY, 0.0f)
	};

	bSuppressRegenerate = true;
	FootprintSpline->ClearSplinePoints(false);
	for (const FVector& P : Points)
	{
		FootprintSpline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
	}
	FootprintSpline->SetClosedLoop(true, false);
	bSuppressRegenerate = false;

	EnforceLinearTangents();
	SyncSplinePointsToReplication();
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::SetSplinePointLocal(int32 PointIndex, const FVector& LocalPos, bool bUpdatePad)
{
	if (!FootprintSpline || PointIndex < 0 || PointIndex >= FootprintSpline->GetNumberOfSplinePoints())
	{
		return;
	}

	FootprintSpline->SetLocationAtSplinePoint(PointIndex, LocalPos, ESplineCoordinateSpace::Local, false);
	EnforceLinearTangents();
	RegenerateBuilding();
	if (bUpdatePad)
	{
		NotifyBuildingChanged();
	}
}

void ATerraDyneBuildingActor::SetSplinePoints(const TArray<FVector>& InPoints)
{
	if (!FootprintSpline || InPoints.Num() < 3)
	{
		return;
	}

	bSuppressRegenerate = true;
	FootprintSpline->ClearSplinePoints(false);
	for (const FVector& Pt : InPoints)
	{
		FootprintSpline->AddSplinePoint(Pt, ESplineCoordinateSpace::Local, false);
	}
	FootprintSpline->SetClosedLoop(true, false);
	FootprintSpline->UpdateSpline();
	bSuppressRegenerate = false;

	EnforceLinearTangents();
	SyncSplinePointsToReplication();
	RegenerateBuilding();
	NotifyBuildingChanged();
}

void ATerraDyneBuildingActor::AddSplinePoint()
{
	if (!FootprintSpline)
	{
		return;
	}

	TArray<FVector> Current = GatherSplinePointsLocal();
	if (Current.Num() < 2)
	{
		return;
	}

	// Insert midpoint on the last edge
	const FVector Mid = (Current.Last() + Current[0]) * 0.5f + FVector(200.0f, 0.0f, 0.0f);
	Current.Add(Mid);
	SetSplinePoints(Current);
}

void ATerraDyneBuildingActor::RemoveSplinePoint(int32 PointIndex)
{
	if (!FootprintSpline)
	{
		return;
	}

	TArray<FVector> Current = GatherSplinePointsLocal();
	if (Current.Num() <= 3)
	{
		// Cannot reduce below 3 points for a closed polygon
		return;
	}

	if (PointIndex >= 0 && PointIndex < Current.Num())
	{
		Current.RemoveAt(PointIndex);
	}
	else
	{
		Current.Pop();
	}

	SetSplinePoints(Current);
}

void ATerraDyneBuildingActor::EnforceLinearTangents()
{
	if (!FootprintSpline)
	{
		return;
	}

	const int32 Num = FootprintSpline->GetNumberOfSplinePoints();
	for (int32 i = 0; i < Num; ++i)
	{
		FootprintSpline->SetSplinePointType(i, ESplinePointType::Linear, false);
	}
	FootprintSpline->UpdateSpline();
}

TArray<FVector> ATerraDyneBuildingActor::GatherSplinePointsLocal() const
{
	TArray<FVector> Points;
	if (!FootprintSpline)
	{
		return Points;
	}

	const int32 Num = FootprintSpline->GetNumberOfSplinePoints();
	Points.Reserve(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		Points.Add(FootprintSpline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local));
	}
	return Points;
}

TArray<FVector2D> ATerraDyneBuildingActor::GetFootprintPolygonLocal() const
{
	TArray<FVector2D> Polygon;
	for (const FVector& P : GatherSplinePointsLocal())
	{
		Polygon.Emplace(P.X, P.Y);
	}
	return Polygon;
}

TArray<FVector2D> ATerraDyneBuildingActor::GetFootprintPolygonWorld() const
{
	const FTransform& Xf = GetActorTransform();

	TArray<FVector2D> Polygon;
	for (const FVector& P : GatherSplinePointsLocal())
	{
		const FVector W = Xf.TransformPosition(FVector(P.X, P.Y, 0.0f));
		Polygon.Emplace(W.X, W.Y);
	}
	return Polygon;
}

FBox2D ATerraDyneBuildingActor::GetFootprintBoundsWorld(float Padding) const
{
	FBox2D Bounds(ForceInit);
	for (const FVector2D& P : GetFootprintPolygonWorld())
	{
		Bounds += P;
	}

	if (Bounds.bIsValid && Padding > 0.0f)
	{
		Bounds = Bounds.ExpandBy(Padding);
	}
	return Bounds;
}

float ATerraDyneBuildingActor::GetBuildingHeight() const
{
	const UTerraDyneBuildingKit* ActiveKit = ResolveKit();
	const float FloorHeight = ActiveKit ? ActiveKit->FloorHeight : 400.0f;
	return FloorHeight * FMath::Max(FloorCount, 0);
}

void ATerraDyneBuildingActor::SetFloorCount(int32 NewCount)
{
	const int32 Clamped = FMath::Max(1, NewCount);
	if (Clamped == FloorCount)
	{
		return;
	}

	FloorCount = Clamped;
	RegenerateBuilding();
}

//--------------------------------------------------------------------------------------------------
// Instancer pool
//--------------------------------------------------------------------------------------------------

UInstancedStaticMeshComponent* ATerraDyneBuildingActor::GetOrCreateInstancer(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}

	for (const TObjectPtr<UInstancedStaticMeshComponent>& Existing : Instancers)
	{
		if (Existing && Existing->GetStaticMesh() == Mesh)
		{
			return Existing;
		}
	}

	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	// Mobility has to be set before registration, otherwise moving the building warns about
	// relocating a static component every time the gizmo drags it.
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetStaticMesh(Mesh);
	ISM->SetupAttachment(SceneRoot);
	ISM->RegisterComponent();
	AddInstanceComponent(ISM);

	Instancers.Add(ISM);
	return ISM;
}

void ATerraDyneBuildingActor::ClearAllInstances()
{
	for (const TObjectPtr<UInstancedStaticMeshComponent>& ISM : Instancers)
	{
		if (ISM)
		{
			ISM->ClearInstances();
		}
	}
}

//--------------------------------------------------------------------------------------------------
// Generation
//--------------------------------------------------------------------------------------------------

void ATerraDyneBuildingActor::RegenerateBuilding()
{
	if (bSuppressRegenerate)
	{
		return;
	}

	ClearAllInstances();

	const UTerraDyneBuildingKit* ActiveKit = ResolveKit();
	if (!ActiveKit || !ActiveKit->IsUsable())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Building %s: kit has no usable wall panels — nothing generated."),
			*GetName());
		return;
	}

	EnforceLinearTangents();

	const TArray<FVector> Points = GatherSplinePointsLocal();
	if (Points.Num() < 2)
	{
		return;
	}

	if (bGenerateWalls)
	{
		BuildWalls(Points, ActiveKit);
	}
	if (bGenerateCornerPosts)
	{
		BuildCornerPosts(Points, ActiveKit);
	}
	if (bGenerateFloors || bGenerateRoof)
	{
		BuildSlabs(Points, ActiveKit);
	}

	if (bIsHolographicPreview)
	{
		ApplyHolographicVisuals();
	}
}

TArray<ATerraDyneBuildingActor::FPanelPlacement> ATerraDyneBuildingActor::PlanSegmentPanels(
	float SegmentLength, const UTerraDyneBuildingKit* InKit) const
{
	TArray<FPanelPlacement> Plan;
	if (!InKit)
	{
		return Plan;
	}

	const TArray<int32> Widths = InKit->GetPanelWidthsDescending();
	const int32 Smallest = InKit->GetSmallestPanelWidth();
	if (Widths.Num() == 0 || Smallest <= 0 || SegmentLength < Smallest - PlacementTolerance)
	{
		return Plan;
	}

	float Offset = 0.0f;
	float Remaining = SegmentLength;

	while (Remaining >= Smallest - PlacementTolerance)
	{
		int32 Chosen = 0;
		for (int32 W : Widths)
		{
			if (W <= Remaining + PlacementTolerance)
			{
				Chosen = W;
				break;
			}
		}

		if (Chosen <= 0)
		{
			break;
		}

		Plan.Add({ Chosen, Offset });
		Offset += Chosen;
		Remaining -= Chosen;
	}

	// A segment that is not a whole number of modules leaves a stub shorter than the narrowest
	// panel. Rather than leave a hole or stretch a mesh, drop one narrow panel flush with the far
	// end — it overlaps its neighbour by less than a module and reads as a seam, not a gap.
	// Footprint grid snap (phase 5) keeps this from firing in normal use.
	if (Remaining > PlacementTolerance)
	{
		Plan.Add({ Smallest, SegmentLength - Smallest });
	}

	return Plan;
}

void ATerraDyneBuildingActor::BuildWalls(const TArray<FVector>& Points, const UTerraDyneBuildingKit* InKit)
{
	const int32 NumPoints = Points.Num();
	const bool bClosed = FootprintSpline && FootprintSpline->IsClosedLoop();
	const int32 NumSegments = bClosed ? NumPoints : NumPoints - 1;

	const int32 DoorWidth = FMath::RoundToInt(InKit->ModuleWidth);
	UStaticMesh* DoorMesh = InKit->WallPanelDoor.IsNull() ? nullptr : InKit->WallPanelDoor.LoadSynchronous();

	for (int32 SegIdx = 0; SegIdx < NumSegments; ++SegIdx)
	{
		const FVector& A = Points[SegIdx];
		const FVector& B = Points[(SegIdx + 1) % NumPoints];

		const FVector2D Delta(B.X - A.X, B.Y - A.Y);
		const float Length = Delta.Size();
		if (Length < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Dir = Delta / Length;
		const FVector Dir3(Dir.X, Dir.Y, 0.0f);

		// Panels run along their local -Y, so the segment's direction angle needs the kit's yaw
		// offset (90 degrees for this pivot convention) to line up. See UTerraDyneBuildingKit.
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + InKit->SegmentYawOffset;
		const FRotator PanelRot(0.0f, Yaw, 0.0f);

		const TArray<FPanelPlacement> Plan = PlanSegmentPanels(Length, InKit);

		bool bDoorPlacedOnSegment = false;

		for (int32 Floor = 0; Floor < FloorCount; ++Floor)
		{
			const float FloorZ = Floor * InKit->FloorHeight;

			for (const FPanelPlacement& Placement : Plan)
			{
				UStaticMesh* Mesh = nullptr;

				const bool bWantDoorHere =
					DoorMesh &&
					Floor == 0 &&
					SegIdx == DoorSegmentIndex &&
					!bDoorPlacedOnSegment &&
					Placement.Width == DoorWidth;

				if (bWantDoorHere)
				{
					Mesh = DoorMesh;
					bDoorPlacedOnSegment = true;
				}
				else
				{
					Mesh = InKit->ResolvePanelOfWidth(Placement.Width);
				}

				if (!Mesh)
				{
					Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				}

				if (Mesh)
				{
					if (UInstancedStaticMeshComponent* ISM = GetOrCreateInstancer(Mesh))
					{
						const bool bIsBasicCube = Mesh->GetPathName().Contains(TEXT("BasicShapes"));
						if (bIsBasicCube)
						{
							const FVector Scale(0.2f, static_cast<float>(Placement.Width) / 100.0f, InKit->FloorHeight / 100.0f);
							const FVector CenterLoc = A + Dir3 * (Placement.Offset + Placement.Width * 0.5f) + FVector(0.0f, 0.0f, FloorZ + InKit->FloorHeight * 0.5f);
							const float WallYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
							ISM->AddInstance(FTransform(FRotator(0.0f, WallYaw, 0.0f), CenterLoc, Scale));
						}
						else
						{
							const FVector Location = A + Dir3 * Placement.Offset + FVector(0.0f, 0.0f, FloorZ);
							ISM->AddInstance(FTransform(PanelRot, Location));
						}
					}
				}
			}
		}
	}
}

void ATerraDyneBuildingActor::BuildCornerPosts(const TArray<FVector>& Points, const UTerraDyneBuildingKit* InKit)
{
	UStaticMesh* PostMesh = InKit->CornerPost.IsNull() ? nullptr : InKit->CornerPost.LoadSynchronous();
	if (!PostMesh)
	{
		PostMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	if (!PostMesh)
	{
		return;
	}

	UInstancedStaticMeshComponent* ISM = GetOrCreateInstancer(PostMesh);
	if (!ISM)
	{
		return;
	}

	const bool bIsBasicCube = PostMesh->GetPathName().Contains(TEXT("BasicShapes"));
	const int32 NumPoints = Points.Num();
	const bool bClosed = FootprintSpline && FootprintSpline->IsClosedLoop();
	const int32 NumPosts = bClosed ? NumPoints : NumPoints;

	for (int32 i = 0; i < NumPosts; ++i)
	{
		// Orient each post along the segment leaving it. The last point of an open run has no
		// outgoing segment, so it borrows the incoming one.
		int32 FromIdx = i;
		int32 ToIdx = (i + 1) % NumPoints;
		if (!bClosed && i == NumPoints - 1)
		{
			FromIdx = i - 1;
			ToIdx = i;
		}

		const FVector2D Delta(Points[ToIdx].X - Points[FromIdx].X, Points[ToIdx].Y - Points[FromIdx].Y);
		if (Delta.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) + InKit->SegmentYawOffset;
		const FRotator PostRot(0.0f, Yaw, 0.0f);

		for (int32 Floor = 0; Floor < FloorCount; ++Floor)
		{
			if (bIsBasicCube)
			{
				const FVector Scale(0.35f, 0.35f, InKit->FloorHeight / 100.0f);
				const FVector CenterLoc = Points[i] + FVector(0.0f, 0.0f, Floor * InKit->FloorHeight + InKit->FloorHeight * 0.5f);
				ISM->AddInstance(FTransform(FRotator(0.0f, Yaw - InKit->SegmentYawOffset, 0.0f), CenterLoc, Scale));
			}
			else
			{
				const FVector Location = Points[i] + FVector(0.0f, 0.0f, Floor * InKit->FloorHeight);
				ISM->AddInstance(FTransform(PostRot, Location));
			}
		}
	}
}

void ATerraDyneBuildingActor::BuildSlabs(const TArray<FVector>& Points, const UTerraDyneBuildingKit* InKit)
{
	TArray<FVector2D> Polygon;
	Polygon.Reserve(Points.Num());
	for (const FVector& P : Points)
	{
		Polygon.Emplace(P.X, P.Y);
	}

	if (Polygon.Num() < 3)
	{
		return;
	}

	const TArray<FTerraDyneFloorTileEntry> FloorSet = InKit->GetFloorTilesDescending();
	const TArray<FTerraDyneFloorTileEntry> RoofSet = InKit->GetRoofTilesDescending();

	// Which storeys get a slab: every floor level, plus the roof one module above the top storey.
	TArray<TPair<float, const TArray<FTerraDyneFloorTileEntry>*>> Levels;
	if (bGenerateFloors && FloorSet.Num() > 0)
	{
		for (int32 Floor = 0; Floor < FloorCount; ++Floor)
		{
			Levels.Emplace(Floor * InKit->FloorHeight, &FloorSet);
		}
	}
	if (bGenerateRoof && RoofSet.Num() > 0)
	{
		Levels.Emplace(FloorCount * InKit->FloorHeight, &RoofSet);
	}

	if (Levels.Num() == 0)
	{
		return;
	}

	FBox2D Bounds(ForceInit);
	for (const FVector2D& P : Polygon)
	{
		Bounds += P;
	}

	if (!Bounds.bIsValid)
	{
		return;
	}

	// Tile pivots sit at a corner with the mesh occupying +X / -Y, so the grid walks X upwards from
	// the low edge and Y downwards from the high edge. Each cell is then filled by PlaceSlabCell,
	// which subdivides rather than letting a full tile hang out past the walls.
	for (const TPair<float, const TArray<FTerraDyneFloorTileEntry>*>& Level : Levels)
	{
		const TArray<FTerraDyneFloorTileEntry>& Tiles = *Level.Value;
		const float TopSize = static_cast<float>(Tiles[0].SizeUnits);
		if (TopSize <= 0.0f)
		{
			continue;
		}

		for (float PX = Bounds.Min.X; PX < Bounds.Max.X - PlacementTolerance; PX += TopSize)
		{
			for (float PY = Bounds.Max.Y; PY > Bounds.Min.Y + PlacementTolerance; PY -= TopSize)
			{
				PlaceSlabCell(Polygon, FVector2D(PX, PY), 0, Tiles, Level.Key);
			}
		}
	}
}

void ATerraDyneBuildingActor::PlaceSlabCell(
	const TArray<FVector2D>& Polygon,
	const FVector2D& CellCorner,
	int32 TileIndex,
	const TArray<FTerraDyneFloorTileEntry>& Tiles,
	float LevelZ)
{
	if (!Tiles.IsValidIndex(TileIndex))
	{
		return;
	}

	const float Size = static_cast<float>(Tiles[TileIndex].SizeUnits);
	if (Size <= 0.0f)
	{
		return;
	}

	if (IsCellInsidePolygon(Polygon, CellCorner, Size))
	{
		UStaticMesh* Mesh = Tiles[TileIndex].Mesh.IsNull() ? nullptr : Tiles[TileIndex].Mesh.LoadSynchronous();
		if (!Mesh)
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}

		if (Mesh)
		{
			if (UInstancedStaticMeshComponent* ISM = GetOrCreateInstancer(Mesh))
			{
				const bool bIsBasicCube = Mesh->GetPathName().Contains(TEXT("BasicShapes"));
				if (bIsBasicCube)
				{
					const FVector Scale(Size / 100.0f, Size / 100.0f, 0.2f);
					const FVector Center(CellCorner.X + Size * 0.5f, CellCorner.Y - Size * 0.5f, LevelZ - 10.0f);
					ISM->AddInstance(FTransform(FRotator::ZeroRotator, Center, Scale));
				}
				else
				{
					ISM->AddInstance(FTransform(FRotator::ZeroRotator,
						FVector(CellCorner.X, CellCorner.Y, LevelZ)));
				}
			}
		}
		return;
	}

	// Not fully inside. Drop to the next tile size down and try again on each sub-cell. At the
	// smallest size a partial cell is simply left empty — the remaining sliver is narrower than
	// the smallest tile and sits under the wall thickness.
	const int32 NextIndex = TileIndex + 1;
	if (!Tiles.IsValidIndex(NextIndex))
	{
		return;
	}

	const float SubSize = static_cast<float>(Tiles[NextIndex].SizeUnits);
	if (SubSize <= 0.0f || SubSize >= Size)
	{
		return;
	}

	const int32 Divisions = FMath::Max(1, FMath::RoundToInt(Size / SubSize));
	for (int32 i = 0; i < Divisions; ++i)
	{
		for (int32 j = 0; j < Divisions; ++j)
		{
			PlaceSlabCell(
				Polygon,
				FVector2D(CellCorner.X + i * SubSize, CellCorner.Y - j * SubSize),
				NextIndex,
				Tiles,
				LevelZ);
		}
	}
}

bool ATerraDyneBuildingActor::IsCellInsidePolygon(
	const TArray<FVector2D>& Polygon, const FVector2D& CellCorner, float Size)
{
	// Corners plus centre, each nudged a hair inwards. On a grid-aligned footprint the cell corners
	// land exactly ON the polygon edge, where a crossing-number test is ambiguous — without the
	// inset the entire perimeter ring of tiles would test as outside and vanish. The inset is far
	// smaller than any real overhang, so a cell that genuinely hangs out still fails.
	const float Inset = FMath::Min(0.5f, Size * 0.01f);
	const float X0 = CellCorner.X + Inset;
	const float X1 = CellCorner.X + Size - Inset;
	const float Y0 = CellCorner.Y - Inset;
	const float Y1 = CellCorner.Y - Size + Inset;

	const FVector2D Samples[5] = {
		FVector2D(X0, Y0),
		FVector2D(X1, Y0),
		FVector2D(X0, Y1),
		FVector2D(X1, Y1),
		FVector2D(CellCorner.X + Size * 0.5f, CellCorner.Y - Size * 0.5f)
	};

	for (const FVector2D& S : Samples)
	{
		if (!IsPointInPolygon(S, Polygon))
		{
			return false;
		}
	}
	return true;
}

bool ATerraDyneBuildingActor::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	// Standard crossing-number test. Works for any simple polygon, convex or not, either winding.
	bool bInside = false;
	const int32 Num = Polygon.Num();

	for (int32 i = 0, j = Num - 1; i < Num; j = i++)
	{
		const FVector2D& Pi = Polygon[i];
		const FVector2D& Pj = Polygon[j];

		if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
			(Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
		{
			bInside = !bInside;
		}
	}

	return bInside;
}

//--------------------------------------------------------------------------------------------------
// Serialization
//--------------------------------------------------------------------------------------------------

FTerraDyneBuildingData ATerraDyneBuildingActor::ToData() const
{
	FTerraDyneBuildingData Data;
	Data.BuildingId = BuildingId;
	Data.Origin = GetActorLocation();
	Data.Rotation = GetActorRotation();
	Data.SplinePoints = GatherSplinePointsLocal();
	Data.bClosedLoop = FootprintSpline ? FootprintSpline->IsClosedLoop() : true;
	Data.FloorCount = FloorCount;
	Data.KitAsset = Kit ? FSoftObjectPath(Kit) : FSoftObjectPath();
	Data.DoorSegmentIndex = DoorSegmentIndex;
	return Data;
}

void ATerraDyneBuildingActor::ApplyData(const FTerraDyneBuildingData& Data, bool bRegenerate)
{
	bSuppressRegenerate = true;

	BuildingId = Data.BuildingId.IsValid() ? Data.BuildingId : FGuid::NewGuid();
	FloorCount = FMath::Max(1, Data.FloorCount);
	DoorSegmentIndex = Data.DoorSegmentIndex;

	SetActorLocationAndRotation(Data.Origin, Data.Rotation);

	if (UObject* Loaded = Data.KitAsset.IsValid() ? Data.KitAsset.TryLoad() : nullptr)
	{
		Kit = Cast<UTerraDyneBuildingKit>(Loaded);
	}

	if (FootprintSpline)
	{
		FootprintSpline->ClearSplinePoints(false);
		for (const FVector& P : Data.SplinePoints)
		{
			FootprintSpline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
		}
		FootprintSpline->SetClosedLoop(Data.bClosedLoop, false);
		FootprintSpline->UpdateSpline();
	}

	bSuppressRegenerate = false;

	if (bRegenerate)
	{
		RegenerateBuilding();
		NotifyBuildingChanged();
	}
}
