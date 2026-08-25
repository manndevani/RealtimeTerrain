// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeRoadSplineActor.h"

#include "RealtimeRoadJunctionActor.h"
#include "RoadMeshBuilder.h"
#include "RoadNetwork.h"
#include "EngineUtils.h"

#include "Components/SplineComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Core/TerraDyneManager.h"
#include "UObject/ConstructorHelpers.h"

ARealtimeRoadSplineActor::ARealtimeRoadSplineActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("SplineComponent"));
	SetRootComponent(SplineComponent);
	SplineComponent->ClearSplinePoints(false);

	RoadMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("RoadMeshComponent"));
	RoadMeshComponent->SetupAttachment(SplineComponent);
	RoadMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RoadMeshComponent->SetGenerateOverlapEvents(false);
	RoadMeshComponent->bEnableComplexCollision = true;

	ScatterHISMComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ScatterHISMComponent"));
	ScatterHISMComponent->SetupAttachment(SplineComponent);
	ScatterHISMComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	// Defaults to the CitySample-styled road materials (Content/UnrealDrive_CitySample) rather than
	// the flat Proto placeholders, since RoadMeshBuilder's UVs are world-scaled and these tile
	// cleanly at that density. Still EditAnywhere, so any road can override back to Proto or
	// anything else per instance.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AsphaltFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Asphalt/MI_FreewayAsphalt_Road.MI_FreewayAsphalt_Road"));
	if (AsphaltFinder.Succeeded()) AsphaltMaterial = AsphaltFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CurbFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Concrete/M_roadcurb_master_concrete_instance.M_roadcurb_master_concrete_instance"));
	if (CurbFinder.Succeeded()) CurbMaterial = CurbFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FootpathFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Sidewalks/MI_Sidewalk.MI_Sidewalk"));
	if (FootpathFinder.Succeeded()) FootpathMaterial = FootpathFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoughFinder(TEXT("/Game/Roads/Materials/M_Proto_RoughRoad.M_Proto_RoughRoad"));
	if (RoughFinder.Succeeded()) RoughRoadMaterial = RoughFinder.Object;

	// Without this, a holographic draft (SetIsHolographicPreview -> ApplyMaterials) had nothing to
	// fall back to and silently kept its normal, opaque material -- a "preview" indistinguishable
	// from a real, already-committed road. See ApplyMaterials's bIsHolographicPreview branch.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HolographicFinder(TEXT("/Game/Materials/M_RoadHolographic.M_RoadHolographic"));
	if (HolographicFinder.Succeeded()) HolographicMaterial = HolographicFinder.Object;
}

void ARealtimeRoadSplineActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARealtimeRoadSplineActor, RoadType);
	DOREPLIFETIME(ARealtimeRoadSplineActor, CrossSectionSettings);
	DOREPLIFETIME(ARealtimeRoadSplineActor, ScatterSettings);
	DOREPLIFETIME(ARealtimeRoadSplineActor, ReplicatedWorldPoints);
	DOREPLIFETIME(ARealtimeRoadSplineActor, JunctionCuts);
}

void ARealtimeRoadSplineActor::BeginPlay()
{
	Super::BeginPlay();

	if (ReplicatedWorldPoints.Num() >= 2)
	{
		SyncSplineFromReplicatedPoints();
	}
	RebuildRoad();
}

void ARealtimeRoadSplineActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SettleTimerHandle);
	}
	ClearTerrainPad();
	Super::EndPlay(EndPlayReason);
}

void ARealtimeRoadSplineActor::Destroyed()
{
	// A road deleted outside of EndPlay — in the editor, say — still has to give the ground back.
	ClearTerrainPad();
	Super::Destroyed();
}

void ARealtimeRoadSplineActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Construction scripts run outside the timer manager's reach, so this always takes the full
	// path rather than scheduling a settle pass that would never fire in the editor.
	RebuildRoad();
}

void ARealtimeRoadSplineActor::OnRep_SplinePoints()
{
	SyncSplineFromReplicatedPoints();
	RebuildRoad();
}

void ARealtimeRoadSplineActor::OnRep_RoadConfig()
{
	RebuildRoad();
}

void ARealtimeRoadSplineActor::OnRep_JunctionCuts()
{
	RebuildRoad();
}

void ARealtimeRoadSplineActor::AddJunctionCut(const FRoadJunctionCut& Cut)
{
	if (!Cut.Junction || Cut.ExitDistance <= Cut.EntryDistance)
	{
		return;
	}

	// One junction owns at most one stretch of a given road. Re-solving the same intersection
	// should move its cut, not accumulate a second one over the top.
	RemoveJunctionCutsFor(Cut.Junction);
	JunctionCuts.Add(Cut);
	RebuildRoad();
}

void ARealtimeRoadSplineActor::RemoveJunctionCutsFor(ARealtimeRoadJunctionActor* Junction)
{
	if (JunctionCuts.RemoveAll([Junction](const FRoadJunctionCut& Cut)
		{
			return Cut.Junction == Junction;
		}) > 0)
	{
		RebuildRoad();
	}
}

bool ARealtimeRoadSplineActor::CanEditPoints() const
{
	// A preview ghost is local-only and never replicates, so any machine may edit its own.
	if (bIsHolographicPreview || HasAuthority())
	{
		return true;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("%s: point edit ignored on a client. Route edits through URealtimeRoadBuilderComponent, ")
		TEXT("which is owned by the player and can reach the server."), *GetName());
	return false;
}

void ARealtimeRoadSplineActor::SyncSplineFromReplicatedPoints()
{
	if (!SplineComponent) return;

	SplineComponent->ClearSplinePoints(false);
	for (int32 i = 0; i < ReplicatedWorldPoints.Num(); ++i)
	{
		SplineComponent->AddSplinePoint(ReplicatedWorldPoints[i], ESplineCoordinateSpace::World, false);
		SplineComponent->SetSplinePointType(i, ESplinePointType::CurveClamped, false);
	}
	SplineComponent->UpdateSpline();
}

void ARealtimeRoadSplineActor::CaptureReplicatedPoints()
{
	if (!HasAuthority() || !SplineComponent) return;

	const int32 Num = SplineComponent->GetNumberOfSplinePoints();
	ReplicatedWorldPoints.Reset(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		ReplicatedWorldPoints.Add(SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
	}
}

void ARealtimeRoadSplineActor::SetIsHolographicPreview(bool bHolographic)
{
	bIsHolographicPreview = bHolographic;

	if (bIsHolographicPreview)
	{
		RoadMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ScatterHISMComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// A ghost is a local visualisation. Replicating it would push half-drawn roads to every
		// other client while the player is still deciding where the road goes.
		if (HasAuthority())
		{
			SetReplicates(false);
		}
	}
	else
	{
		RoadMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ScatterHISMComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		if (HasAuthority())
		{
			SetReplicates(true);
			CaptureReplicatedPoints();
		}
	}

	ApplyMaterials();
	RebuildRoad();
}

//--------------------------------------------------------------------------------------------
// Point editing
//--------------------------------------------------------------------------------------------

void ARealtimeRoadSplineActor::AddPointRealtime(const FVector& WorldLocation)
{
	if (!SplineComponent || !CanEditPoints()) return;

	const int32 NewIndex = SplineComponent->GetNumberOfSplinePoints();
	SplineComponent->AddSplinePoint(WorldLocation, ESplineCoordinateSpace::World, false);
	SplineComponent->SetSplinePointType(NewIndex, ESplinePointType::CurveClamped, true);

	CaptureReplicatedPoints();
	RequestRebuild();
}

void ARealtimeRoadSplineActor::SetPointsRealtime(const TArray<FVector>& WorldPoints)
{
	if (!SplineComponent || !CanEditPoints()) return;

	SplineComponent->ClearSplinePoints(false);
	for (int32 i = 0; i < WorldPoints.Num(); ++i)
	{
		SplineComponent->AddSplinePoint(WorldPoints[i], ESplineCoordinateSpace::World, false);
		SplineComponent->SetSplinePointType(i, ESplinePointType::CurveClamped, false);
	}
	SplineComponent->UpdateSpline();

	CaptureReplicatedPoints();
	RequestRebuild();
}

void ARealtimeRoadSplineActor::MovePointRealtime(int32 PointIndex, const FVector& NewWorldLocation)
{
	if (!SplineComponent || !CanEditPoints()) return;
	if (PointIndex < 0 || PointIndex >= SplineComponent->GetNumberOfSplinePoints()) return;

	SplineComponent->SetLocationAtSplinePoint(PointIndex, NewWorldLocation, ESplineCoordinateSpace::World, true);

	CaptureReplicatedPoints();
	RequestRebuild();
}

void ARealtimeRoadSplineActor::ClearPointsRealtime()
{
	if (!SplineComponent || !CanEditPoints()) return;

	SplineComponent->ClearSplinePoints(true);
	if (HasAuthority())
	{
		ReplicatedWorldPoints.Reset();
	}

	if (RoadMeshComponent && RoadMeshComponent->GetDynamicMesh())
	{
		RoadMeshComponent->GetDynamicMesh()->Reset();
		RoadMeshComponent->NotifyMeshUpdated();
	}
	if (ScatterHISMComponent)
	{
		ScatterHISMComponent->ClearInstances();
	}
}

int32 ARealtimeRoadSplineActor::InsertPointAtDistance(float Distance)
{
	if (!SplineComponent || !CanEditPoints()) return INDEX_NONE;

	const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
	if (NumPoints < 2) return INDEX_NONE;

	const double SplineLength = SplineComponent->GetSplineLength();
	const double Clamped = FMath::Clamp(static_cast<double>(Distance), 0.0, SplineLength);
	const FVector Location = SplineComponent->GetLocationAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World);

	// Landing on top of an existing point means the caller already has the anchor it wants.
	const double MergeRadiusSq = FMath::Square(FMath::Max(100.0f, CrossSectionSettings.RoadWidth * 0.25f));
	for (int32 i = 0; i < NumPoints; ++i)
	{
		const FVector Existing = SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		if (FVector::DistSquared(Existing, Location) < MergeRadiusSq)
		{
			return i;
		}
	}

	int32 InsertIndex = NumPoints;
	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (SplineComponent->GetDistanceAlongSplineAtSplinePoint(i) >= Clamped)
		{
			InsertIndex = i;
			break;
		}
	}

	TArray<FVector> Points;
	Points.Reserve(NumPoints + 1);
	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (i == InsertIndex)
		{
			Points.Add(Location);
		}
		Points.Add(SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
	}
	if (InsertIndex >= NumPoints)
	{
		Points.Add(Location);
	}

	SetPointsRealtime(Points);
	return InsertIndex;
}

int32 ARealtimeRoadSplineActor::FindClosestSplinePoint(const FVector& WorldLocation, float& OutDistance) const
{
	OutDistance = MAX_FLT;
	if (!SplineComponent) return INDEX_NONE;

	const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
	int32 BestIndex = INDEX_NONE;

	for (int32 i = 0; i < NumPoints; ++i)
	{
		const FVector PointLocation = SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		const float Distance = FVector::Dist(WorldLocation, PointLocation);
		if (Distance < OutDistance)
		{
			OutDistance = Distance;
			BestIndex = i;
		}
	}

	return BestIndex;
}

FVector ARealtimeRoadSplineActor::GetSplinePointWorldLocation(int32 PointIndex) const
{
	if (!SplineComponent || PointIndex < 0 || PointIndex >= SplineComponent->GetNumberOfSplinePoints())
	{
		return GetActorLocation();
	}
	return SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
}

ARealtimeRoadSplineActor* ARealtimeRoadSplineActor::ExtendFromPointToLocation(int32 FromPointIndex, const FVector& TargetWorldLocation, int32& OutSelectedPointIndex, float MaxSegmentSpacing)
{
	OutSelectedPointIndex = INDEX_NONE;
	if (!SplineComponent || !CanEditPoints()) return nullptr;

	const int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
	if (NumPoints == 0)
	{
		AddPointRealtime(TargetWorldLocation);
		OutSelectedPointIndex = 0;
		return this;
	}

	const int32 SafeIndex = FMath::Clamp(FromPointIndex, 0, NumPoints - 1);

	// An interior point cannot simply be extended: the road already continues past it, so the
	// only coherent interpretation is a new branch. The branch's own tip is what should end up
	// selected, not anything on this (unchanged, aside from gaining a junction) road.
	if (SafeIndex != 0 && SafeIndex != NumPoints - 1)
	{
		ARealtimeRoadSplineActor* BranchRoad = BranchRoadFromPoint(SafeIndex, TargetWorldLocation, MaxSegmentSpacing);
		if (BranchRoad && BranchRoad->SplineComponent)
		{
			OutSelectedPointIndex = BranchRoad->SplineComponent->GetNumberOfSplinePoints() - 1;
		}
		return BranchRoad;
	}

	const FVector StartPos = SplineComponent->GetLocationAtSplinePoint(SafeIndex, ESplineCoordinateSpace::World);
	const double TotalDistance = FVector::Dist2D(StartPos, TargetWorldLocation);
	const double Spacing = FMath::Max(200.0f, MaxSegmentSpacing);
	const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(TotalDistance / Spacing));

	// Control points only need to trace the route. Height comes from the per-station terrain
	// sampling in BuildConformedCentreline, so these are seeded with the ground height purely so
	// an un-conformed road still looks sane.
	TArray<FVector> NewPoints;
	NewPoints.Reserve(NumSteps);
	for (int32 i = 1; i <= NumSteps; ++i)
	{
		const double Alpha = static_cast<double>(i) / static_cast<double>(NumSteps);
		FVector Interpolated = FMath::Lerp(StartPos, TargetWorldLocation, Alpha);

		float GroundZ = 0.0f;
		if (SampleTerrainHeightAtXY(FVector2D(Interpolated.X, Interpolated.Y), GroundZ))
		{
			Interpolated.Z = GroundZ + TerrainSurfaceOffset;
		}
		NewPoints.Add(Interpolated);
	}

	TArray<FVector> Existing;
	Existing.Reserve(NumPoints);
	for (int32 i = 0; i < NumPoints; ++i)
	{
		Existing.Add(SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
	}

	const bool bAppending = (SafeIndex == NumPoints - 1);

	TArray<FVector> Combined;
	if (bAppending)
	{
		Combined = MoveTemp(Existing);
		Combined.Append(NewPoints);
	}
	else
	{
		// Prepending: the generated run heads outward from the old start, so it goes on reversed.
		Combined.Reserve(NewPoints.Num() + Existing.Num());
		for (int32 i = NewPoints.Num() - 1; i >= 0; --i)
		{
			Combined.Add(NewPoints[i]);
		}
		Combined.Append(Existing);
	}

	SetPointsRealtime(Combined);

	// The grown end is exactly what SafeIndex named as the extension point -- append grows past
	// the old last index, prepend grows past the old first (which is still index 0 afterward,
	// since the new run was inserted before it).
	OutSelectedPointIndex = bAppending ? (Combined.Num() - 1) : 0;
	return this;
}

ARealtimeRoadSplineActor* ARealtimeRoadSplineActor::BranchRoadFromPoint(int32 PointIndex, const FVector& BranchTargetLocation, float MaxSegmentSpacing)
{
	UWorld* World = GetWorld();
	if (!World || !SplineComponent) return nullptr;
	if (PointIndex < 0 || PointIndex >= SplineComponent->GetNumberOfSplinePoints()) return nullptr;

	const FVector Origin = SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
	const FVector BranchDirection = (BranchTargetLocation - Origin).GetSafeNormal2D();
	if (BranchDirection.IsNearlyZero()) return nullptr;

	const float BranchHeading = FRotator::ClampAxis(
		FMath::RadiansToDegrees(FMath::Atan2(BranchDirection.Y, BranchDirection.X)));

	// The junction owns the geometry decisions — how many legs, how long the arms have to be for
	// the fillets to fit, and cutting this road around its footprint so it stops running straight
	// through the intersection.
	const double Distance = SplineComponent->GetDistanceAlongSplineAtSplinePoint(PointIndex);

	TArray<int32> ExtraLegs;
	ARealtimeRoadJunctionActor* Junction =
		RoadNetwork::CreateJunctionOnRoad(this, Distance, { BranchHeading }, ExtraLegs);

	if (!Junction || ExtraLegs.Num() == 0) return nullptr;

	const int32 BranchLeg = ExtraLegs[0];

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARealtimeRoadSplineActor* BranchRoad = World->SpawnActor<ARealtimeRoadSplineActor>(
		ARealtimeRoadSplineActor::StaticClass(), Origin, FRotator::ZeroRotator, SpawnParams);
	if (!BranchRoad) return nullptr;

	BranchRoad->RoadType = RoadType;
	BranchRoad->CrossSectionSettings = CrossSectionSettings;
	BranchRoad->ScatterSettings = ScatterSettings;
	BranchRoad->bConformToTerrainInRealtime = bConformToTerrainInRealtime;
	BranchRoad->TerrainSurfaceOffset = TerrainSurfaceOffset;
	BranchRoad->TerrainSmoothingSamples = TerrainSmoothingSamples;
	BranchRoad->StartJunction = Junction;
	BranchRoad->StartJunctionLegIndex = BranchLeg;

	FVector StartPos = Origin;
	FTransform LegSocket;
	if (Junction->GetLegConnectionTransform(BranchLeg, LegSocket))
	{
		StartPos = LegSocket.GetLocation();
	}

	BranchRoad->SetPointsRealtime({ StartPos });
	int32 UnusedIndex = INDEX_NONE;
	BranchRoad->ExtendFromPointToLocation(0, BranchTargetLocation, UnusedIndex, MaxSegmentSpacing);

	return BranchRoad;
}

//--------------------------------------------------------------------------------------------
// Terrain
//--------------------------------------------------------------------------------------------

bool ARealtimeRoadSplineActor::SampleTerrainHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const
{
	return RoadMesh::SampleGroundHeight(this, CachedTerraDyneManager, WorldXY, OutHeightZ);
}

bool ARealtimeRoadSplineActor::SampleTerrainHeightAtXYWithSource(const FVector2D& WorldXY, float& OutHeightZ, bool& OutWasNonTerraDyneGround) const
{
	return RoadMesh::SampleGroundHeight(this, CachedTerraDyneManager, WorldXY, OutHeightZ, &OutWasNonTerraDyneGround);
}

void ARealtimeRoadSplineActor::PinRunEnds(FRoadRun& Run) const
{
	if (Run.Points.Num() < 2) return;

	const int32 Blend = FMath::Clamp(JunctionBlendStations, 1, Run.Points.Num() - 1);

	auto Pin = [&Run, Blend](const FVector& Target, bool bFromStart)
	{
		const int32 AnchorIndex = bFromStart ? 0 : Run.Points.Num() - 1;
		const FVector Delta = Target - Run.Points[AnchorIndex];
		if (Delta.IsNearlyZero()) return;

		// Full correction at the pinned end, tapering to nothing Blend stations in, so the run
		// meets the socket exactly without a visible crease.
		for (int32 i = 0; i <= Blend; ++i)
		{
			const int32 Index = bFromStart ? i : Run.Points.Num() - 1 - i;
			const double Alpha = 1.0 - static_cast<double>(i) / static_cast<double>(Blend);
			Run.Points[Index] += Delta * Alpha;
		}
	};

	FTransform Socket;
	if (Run.StartPinJunction && Run.StartPinJunction->GetLegConnectionTransform(Run.StartPinLeg, Socket))
	{
		Pin(Socket.GetLocation(), true);
	}
	if (Run.EndPinJunction && Run.EndPinJunction->GetLegConnectionTransform(Run.EndPinLeg, Socket))
	{
		Pin(Socket.GetLocation(), false);
	}
}

void ARealtimeRoadSplineActor::BuildRuns(
	const TArray<FVector>& Points,
	const TArray<double>& Distances,
	TArray<FRoadRun>& OutRuns) const
{
	OutRuns.Reset();
	if (Points.Num() < 2 || Points.Num() != Distances.Num()) return;

	// Cuts arrive in whatever order the solver produced them; walking the stations once requires
	// them in route order.
	TArray<FRoadJunctionCut> Ordered;
	Ordered.Reserve(JunctionCuts.Num());
	for (const FRoadJunctionCut& Cut : JunctionCuts)
	{
		if (Cut.Junction && Cut.ExitDistance > Cut.EntryDistance)
		{
			Ordered.Add(Cut);
		}
	}
	Ordered.Sort([](const FRoadJunctionCut& A, const FRoadJunctionCut& B)
	{
		return A.EntryDistance < B.EntryDistance;
	});

	int32 Station = 0;
	FRoadRun Current;
	Current.StartPinJunction = StartJunction;
	Current.StartPinLeg = StartJunctionLegIndex;

	for (const FRoadJunctionCut& Cut : Ordered)
	{
		while (Station < Points.Num() && Distances[Station] < Cut.EntryDistance)
		{
			Current.Points.Add(Points[Station]);
			++Station;
		}

		// A run of one station cannot be swept; a junction sitting on the very end of a road
		// legitimately produces one.
		if (Current.Points.Num() >= 2)
		{
			Current.EndPinJunction = Cut.Junction;
			Current.EndPinLeg = Cut.EntryLegIndex;
			OutRuns.Add(MoveTemp(Current));
		}

		Current = FRoadRun();
		Current.StartPinJunction = Cut.Junction;
		Current.StartPinLeg = Cut.ExitLegIndex;

		// Skip everything the junction slab covers.
		while (Station < Points.Num() && Distances[Station] <= Cut.ExitDistance)
		{
			++Station;
		}
	}

	while (Station < Points.Num())
	{
		Current.Points.Add(Points[Station]);
		++Station;
	}

	if (Current.Points.Num() >= 2)
	{
		Current.EndPinJunction = EndJunction;
		Current.EndPinLeg = EndJunctionLegIndex;
		OutRuns.Add(MoveTemp(Current));
	}

	for (FRoadRun& Run : OutRuns)
	{
		PinRunEnds(Run);
	}
}

bool ARealtimeRoadSplineActor::BuildConformedCentreline(
	TArray<FVector>& OutPoints,
	TArray<double>& OutDistances) const
{
	OutPoints.Reset();
	OutDistances.Reset();
	if (!SplineComponent || SplineComponent->GetNumberOfSplinePoints() < 2) return false;

	const double SplineLength = SplineComponent->GetSplineLength();
	if (SplineLength < 10.0) return false;

	const double Step = FMath::Clamp(static_cast<double>(CrossSectionSettings.SegmentStepLength), 40.0, 600.0);
	const int32 NumSegments = FMath::Max(2, FMath::CeilToInt(SplineLength / Step));
	const double ActualStep = SplineLength / static_cast<double>(NumSegments);

	OutPoints.Reserve(NumSegments + 1);
	OutDistances.Reserve(NumSegments + 1);

	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const double Distance = FMath::Min(i * ActualStep, SplineLength);
		FVector Position = SplineComponent->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		if (bConformToTerrainInRealtime)
		{
			float GroundZ = 0.0f;
			if (SampleTerrainHeightAtXY(FVector2D(Position.X, Position.Y), GroundZ))
			{
				Position.Z = GroundZ;
			}
		}

		OutPoints.Add(Position);
		OutDistances.Add(Distance);
	}

	if (bConformToTerrainInRealtime)
	{
		RoadMesh::SmoothHeights(OutPoints, TerrainSmoothingSamples);

		for (FVector& Point : OutPoints)
		{
			Point.Z += TerrainSurfaceOffset;
		}
	}

	// Junction pinning happens per run in BuildRuns, after the stations have been cut, so that
	// smoothing cannot drag a socket-pinned end off its socket.
	return OutPoints.Num() >= 2;
}

void ARealtimeRoadSplineActor::ComputeCrossSlopeRoll(const TArray<RoadMesh::FFrame>& Frames, TArray<double>& OutRoll) const
{
	OutRoll.Reset();
	OutRoll.SetNumZeroed(Frames.Num());

	if (!bConformToTerrainInRealtime)
	{
		return;
	}

	// Two distinct policies share this function, both expressed as "which stations are even allowed
	// a nonzero roll, and how far can it go":
	//  - Rough roads bank everywhere (their own toggle/ceiling), full stop -- unchanged from before.
	//  - Paved roads stay level over TerraDyne's own terrain by design (see bRoughRoadFollowsCrossSlope's
	//    comment), but get a small, separately-ceilinged tilt specifically where BOTH lateral samples
	//    land on ground with no TerraDyne chunk under it -- in practice, a locally-imported Cesium
	//    tile terrain, whose real relief can be steeper than anything TerraDyne's own terrain
	//    produces. One sample on TerraDyne ground and the other on Cesium ground (straddling the
	//    cutout seam) is treated as "not banking" -- tilting toward whichever side happened to be
	//    Cesium would be an artifact of where that seam sits, not a real cross-slope.
	const bool bRoughBanking = (RoadType == ERoadType::RoughRoad) && bRoughRoadFollowsCrossSlope;
	const bool bPavedCesiumBanking = (RoadType == ERoadType::PavedRoad);
	if (!bRoughBanking && !bPavedCesiumBanking)
	{
		return;
	}

	const double HalfWidth = FMath::Max(50.0f, CrossSectionSettings.RoadWidth) * 0.5;
	const double MaxRoll = FMath::DegreesToRadians(FMath::Clamp(
		bRoughBanking ? MaxCrossSlopeDegrees : MaxPavedCrossSlopeDegrees, 0.0f, 60.0f));

	for (int32 i = 0; i < Frames.Num(); ++i)
	{
		const FVector Left = Frames[i].Origin - Frames[i].Right * HalfWidth;
		const FVector Right = Frames[i].Origin + Frames[i].Right * HalfWidth;

		float LeftZ = 0.0f;
		float RightZ = 0.0f;
		bool bLeftIsNonTerraDyne = false;
		bool bRightIsNonTerraDyne = false;
		const bool bGotLeft = SampleTerrainHeightAtXYWithSource(FVector2D(Left.X, Left.Y), LeftZ, bLeftIsNonTerraDyne);
		const bool bGotRight = SampleTerrainHeightAtXYWithSource(FVector2D(Right.X, Right.Y), RightZ, bRightIsNonTerraDyne);
		if (!bGotLeft || !bGotRight)
		{
			continue;
		}

		if (bPavedCesiumBanking && !(bLeftIsNonTerraDyne && bRightIsNonTerraDyne))
		{
			continue;
		}

		// A positive rotation about the tangent lifts the +Right edge, so the sign follows the
		// height difference directly.
		const double Roll = FMath::Atan2(static_cast<double>(RightZ - LeftZ), 2.0 * HalfWidth);
		OutRoll[i] = FMath::Clamp(Roll, -MaxRoll, MaxRoll);
	}

	// Lateral samples are as noisy as the centreline ones; without this the track visibly shimmies.
	const int32 HalfWindow = FMath::Clamp(TerrainSmoothingSamples, 0, 20);
	if (HalfWindow > 0 && OutRoll.Num() > 2)
	{
		const TArray<double> Source = OutRoll;
		for (int32 i = 0; i < OutRoll.Num(); ++i)
		{
			const int32 Begin = FMath::Max(0, i - HalfWindow);
			const int32 End = FMath::Min(OutRoll.Num() - 1, i + HalfWindow);

			double Sum = 0.0;
			for (int32 k = Begin; k <= End; ++k)
			{
				Sum += Source[k];
			}
			OutRoll[i] = Sum / static_cast<double>(End - Begin + 1);
		}
	}
}

//--------------------------------------------------------------------------------------------
// Mesh generation
//--------------------------------------------------------------------------------------------

void ARealtimeRoadSplineActor::RebuildRoad()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SettleTimerHandle);
	}
	RebuildInternal(true);
}

void ARealtimeRoadSplineActor::RequestRebuild()
{
	RebuildInternal(false);

	UWorld* World = GetWorld();
	if (!World) return;

	// Collision cooking is the expensive half of a rebuild, so it waits until the edits stop.
	World->GetTimerManager().SetTimer(
		SettleTimerHandle, this, &ARealtimeRoadSplineActor::RebuildRoad,
		FMath::Max(0.01f, SettleDelaySeconds), false);
}

void ARealtimeRoadSplineActor::RebuildInternal(bool bUpdateCollision)
{
	if (!RoadMeshComponent) return;

	ApplyMaterials();
	GenerateRoadMesh(bUpdateCollision);
	OnRoadMeshUpdated.Broadcast(this);
}

void ARealtimeRoadSplineActor::ApplyMaterials()
{
	if (!RoadMeshComponent) return;

	// ConfigureMaterialSet sizes the slot array to match the material IDs the mesh actually writes.
	// Assigning slots one at a time left the component reporting a single slot, so curb and
	// footpath triangles fell back to the asphalt material.
	TArray<UMaterialInterface*> MaterialSet;
	MaterialSet.SetNum(RoadMesh::Slots::Count);

	if (bIsHolographicPreview && HolographicMaterial)
	{
		for (UMaterialInterface*& Slot : MaterialSet)
		{
			Slot = HolographicMaterial;
		}
	}
	else if (RoadType == ERoadType::PavedRoad)
	{
		MaterialSet[RoadMesh::Slots::Surface] = AsphaltMaterial;
		MaterialSet[RoadMesh::Slots::Curb] = CurbMaterial;
		MaterialSet[RoadMesh::Slots::Footpath] = FootpathMaterial;
	}
	else
	{
		for (UMaterialInterface*& Slot : MaterialSet)
		{
			Slot = RoughRoadMaterial;
		}
	}

	RoadMeshComponent->ConfigureMaterialSet(MaterialSet);
}

void ARealtimeRoadSplineActor::GenerateRoadMesh(bool bUpdateCollision)
{
	UDynamicMesh* DynamicMesh = RoadMeshComponent->GetDynamicMesh();
	if (!DynamicMesh) return;

	TArray<FVector> Centreline;
	TArray<double> Distances;
	TArray<FRoadRun> Runs;

	if (BuildConformedCentreline(Centreline, Distances))
	{
		BuildRuns(Centreline, Distances, Runs);
	}

	if (Runs.Num() == 0)
	{
		DynamicMesh->Reset();
		RoadMeshComponent->NotifyMeshUpdated();
		if (ScatterHISMComponent)
		{
			ScatterHISMComponent->ClearInstances();
		}
		return;
	}

	RoadMesh::FSection Section;
	RoadMesh::BuildRoadSection(CrossSectionSettings, RoadType == ERoadType::PavedRoad, Section);

	RoadMesh::FSweepOptions Options;
	Options.UVScale = FMath::Max(1.0e-5f, CrossSectionSettings.UVTilingScale);
	Options.bSectionIsClosed = true;
	Options.bCapStart = true;
	Options.bCapEnd = true;

	// A curb+footpath band swept at a fixed Lateral independently at every station is a naive
	// per-vertex-normal offset -- it bowties through itself on the inside of any turn tighter than
	// the band is wide. BandInnerHalfWidth marks where the always-full-width road surface ends and
	// the scalable band begins; the per-frame scale itself is computed below, once Frames exist for
	// each run, from how sharply the road is actually turning at each station.
	Options.BandInnerHalfWidth = FMath::Max(50.0f, CrossSectionSettings.RoadWidth) * 0.5;
	double RequestedBandWidth = 0.0;
	if (RoadType == ERoadType::PavedRoad)
	{
		if (CrossSectionSettings.bHasCurbs) RequestedBandWidth += FMath::Max(0.0f, CrossSectionSettings.CurbWidth);
		if (CrossSectionSettings.bHasFootpaths) RequestedBandWidth += FMath::Max(0.0f, CrossSectionSettings.FootpathWidth);
	}

	const FTransform WorldToLocal = RoadMeshComponent->GetComponentTransform().Inverse();

	// Deferring the collision cook is what makes dragging a point interactive; the settle pass
	// clears the flag and forces the rebuild once edits stop.
	RoadMeshComponent->SetDeferredCollisionUpdatesEnabled(!bUpdateCollision, false);

	TArray<TArray<RoadMesh::FFrame>> RunFrames;
	RunFrames.Reserve(Runs.Num());

	DynamicMesh->Reset();
	DynamicMesh->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		// Every run is swept into the same mesh, so a road crossing three junctions is four
		// disconnected pieces of geometry on one component rather than four actors.
		for (const FRoadRun& Run : Runs)
		{
			TArray<RoadMesh::FFrame> Frames;
			if (!RoadMesh::BuildFramesFromPolyline(Run.Points, Frames))
			{
				continue;
			}

			TArray<double> Roll;
			ComputeCrossSlopeRoll(Frames, Roll);
			RoadMesh::ApplyRoll(Frames, Roll);

			RoadMesh::ComputeBandScaleFromCurvature(
				Frames, RequestedBandWidth, RoadMesh::BandRadiusSafetyFactor, Options.BandScalePerFrame);

			RoadMesh::SweepSection(Mesh, Section, Frames, WorldToLocal, Options);
			RunFrames.Add(MoveTemp(Frames));
		}
	});

	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
		DynamicMesh,
		FGeometryScriptSplitNormalsOptions(),
		FGeometryScriptCalculateNormalsOptions());

	RoadMeshComponent->NotifyMeshUpdated();

	if (bUpdateCollision)
	{
		RoadMeshComponent->SetDeferredCollisionUpdatesEnabled(false, true);
	}

	// Scatter is per-instance HISM: every regenerate clears and re-adds every prop along the whole
	// road, each with its own terrain-height sample. Repeating that on every frame of a drag is a
	// lot of work for cosmetic detail, so it waits for the settle pass like the terrain pad below.
	//
	// But simply skipping the regenerate would leave the previous frame's instances sitting where
	// the road USED to be -- props stranded off the road, drifting further from it the longer the
	// drag goes, which reads as broken geometry rather than as detail that hasn't caught up yet.
	// Clearing instead is unambiguous: the road surface, curbs and junctions still update live every
	// frame (that is the shape the player is actually judging), and the decorative scatter simply
	// isn't drawn until the edit settles, at which point it reappears correctly placed.
	if (bUpdateCollision)
	{
		GenerateScatter(RunFrames);
	}
	else if (ScatterHISMComponent && ScatterHISMComponent->GetInstanceCount() > 0)
	{
		ScatterHISMComponent->ClearInstances();
	}

	// Rebasing rasterises into every chunk the road touches and rebuilds their meshes, so it waits
	// for the settle pass rather than running on each frame of a drag.
	if (bUpdateCollision)
	{
		UpdateTerrainPad(RunFrames);
	}
}

void ARealtimeRoadSplineActor::RefreshAffectedJunctions(bool bFullNetworkReconcile)
{
	// A junction that is mid-solve is the reason this road is rebuilding at all; re-solving from
	// here would recurse.
	if (ARealtimeRoadJunctionActor::IsAnySolveInProgress())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !SplineComponent || bIsHolographicPreview) return;

	const double Length = SplineComponent->GetSplineLength();
	if (Length < 10.0) return;

	TSet<ARealtimeRoadJunctionActor*> Affected;

	// Junctions this road is already attached to follow it even if the route has moved away.
	for (const FRoadJunctionCut& Cut : JunctionCuts)
	{
		if (Cut.Junction) Affected.Add(Cut.Junction);
	}
	if (StartJunction) Affected.Add(StartJunction);
	if (EndJunction) Affected.Add(EndJunction);

	// Plus any whose bounds the route now reaches, so a road dragged into one is adopted by it.
	for (TActorIterator<ARealtimeRoadJunctionActor> It(World); It; ++It)
	{
		ARealtimeRoadJunctionActor* Junction = *It;
		if (!Junction || Junction->bIsHolographicPreview || !Junction->bSolveFromNearbyRoads)
		{
			continue;
		}
		if (Affected.Contains(Junction))
		{
			continue;
		}

		const FVector Centre = Junction->GetActorLocation();
		const float Key = SplineComponent->FindInputKeyClosestToWorldLocation(Centre);
		const FVector Closest = SplineComponent->GetLocationAtDistanceAlongSpline(
			SplineComponent->GetDistanceAlongSplineAtSplineInputKey(Key), ESplineCoordinateSpace::World);

		if (FVector::Dist2D(Closest, Centre) <= Junction->BoundsRadius)
		{
			Affected.Add(Junction);
		}
	}

	for (ARealtimeRoadJunctionActor* Junction : Affected)
	{
		if (Junction && !Junction->IsActorBeingDestroyed())
		{
			Junction->RebuildJunction();
		}
	}

	// Then let the detector reconcile crossings, so a road dragged across another creates the
	// crossing rather than only refreshing intersections it already belonged to. The interactive
	// drag path scopes this to crossings involving just this road (see the header comment) --
	// re-solving every auto-generated junction in the level ~12 times a second regardless of
	// whether the drag was anywhere near them is work whose cost grows with the whole level rather
	// than with the edit. Every commit-time caller still gets the full, authoritative
	// reconciliation, so this only ever narrows what the interactive path re-solves, never what the
	// final committed state is.
	if (bFullNetworkReconcile)
	{
		RoadNetwork::RebuildIntersections(World);
	}
	else
	{
		RoadNetwork::RebuildIntersectionsNear(World, this);
	}
}

void ARealtimeRoadSplineActor::ClearTerrainPad()
{
	ATerraDyneManager* Manager = CachedTerraDyneManager.Get();
	if (!Manager)
	{
		return;
	}

	const FBox2D Bounds = Manager->GetExternalPadBounds(this);
	Manager->ClearExternalPadStamps(this);

	if (Bounds.bIsValid)
	{
		Manager->RefreshBuildingPads(Bounds);
	}
}

void ARealtimeRoadSplineActor::UpdateTerrainPad(const TArray<TArray<RoadMesh::FFrame>>& RunFrames)
{
	// Resolving through the sampler keeps the manager cache warm and shared with height queries.
	float Unused = 0.0f;
	SampleTerrainHeightAtXY(FVector2D(GetActorLocation().X, GetActorLocation().Y), Unused);

	ATerraDyneManager* Manager = CachedTerraDyneManager.Get();
	if (!Manager)
	{
		return;
	}

	// The union of where the pad was and where it now is, so ground the road has moved off is
	// recomposed too — otherwise a moved road leaves its old trench behind.
	const FBox2D PreviousBounds = Manager->GetExternalPadBounds(this);

	TArray<FTerraDynePadStamp> Stamps;

	if (bRebaseTerrain && !bIsHolographicPreview)
	{
		const double HalfWidth =
			FMath::Max(50.0f, CrossSectionSettings.RoadWidth) * 0.5
			+ FMath::Max(0.0f, CrossSectionSettings.bHasCurbs ? CrossSectionSettings.CurbWidth : 0.0f)
			+ FMath::Max(0.0f, CrossSectionSettings.bHasFootpaths ? CrossSectionSettings.FootpathWidth : 0.0f)
			+ FMath::Max(0.0f, PadWidthMargin);

		const double Depth = FMath::Max(2.0f, CrossSectionSettings.RoadThickness) + FMath::Max(0.0f, PadDepthBelowSurface);
		const double Spacing = FMath::Max(100.0f, PadSampleSpacing);

		for (const TArray<RoadMesh::FFrame>& Frames : RunFrames)
		{
			if (Frames.Num() < 2)
			{
				continue;
			}

			const RoadMesh::FFrame* Anchor = &Frames[0];

			for (int32 i = 1; i < Frames.Num(); ++i)
			{
				const RoadMesh::FFrame& Current = Frames[i];
				const bool bFinal = (i == Frames.Num() - 1);

				if (!bFinal && (Current.DistanceAlong - Anchor->DistanceAlong) < Spacing)
				{
					continue;
				}

				const FVector AnchorLeft = Anchor->Origin - Anchor->Right * HalfWidth;
				const FVector AnchorRight = Anchor->Origin + Anchor->Right * HalfWidth;
				const FVector CurrentLeft = Current.Origin - Current.Right * HalfWidth;
				const FVector CurrentRight = Current.Origin + Current.Right * HalfWidth;

				FTerraDynePadStamp Stamp;
				Stamp.Falloff = FMath::Max(1.0f, PadFalloff);

				// The lower of the two ends, never the mean.
				//
				// A pad quad is level while the road above it slopes, so a quad set to the mean
				// sits half the span's drop ABOVE the road at the downhill end. With the slab only
				// 33 cm thick that broke through on any grade steeper than about 1 percent, which
				// is what put regular bands of terrain through the road surface. Taking the
				// minimum guarantees the pad is at or below the road everywhere in the span.
				const double SpanLow = FMath::Min(Anchor->Origin.Z, Current.Origin.Z);
				Stamp.TargetWorldZ = static_cast<float>(SpanLow - Depth);

				Stamp.PolygonWorldXY.Reserve(4);
				Stamp.PolygonWorldXY.Emplace(AnchorLeft.X, AnchorLeft.Y);
				Stamp.PolygonWorldXY.Emplace(AnchorRight.X, AnchorRight.Y);
				Stamp.PolygonWorldXY.Emplace(CurrentRight.X, CurrentRight.Y);
				Stamp.PolygonWorldXY.Emplace(CurrentLeft.X, CurrentLeft.Y);

				Stamps.Add(MoveTemp(Stamp));
				Anchor = &Current;
			}
		}
	}

	Manager->SetExternalPadStamps(this, Stamps);

	FBox2D Refresh = Manager->GetExternalPadBounds(this);
	if (PreviousBounds.bIsValid)
	{
		Refresh = Refresh.bIsValid ? (Refresh + PreviousBounds) : PreviousBounds;
	}

	if (Refresh.bIsValid)
	{
		Manager->RefreshBuildingPads(Refresh);
	}
}

void ARealtimeRoadSplineActor::GenerateScatter(const TArray<TArray<RoadMesh::FFrame>>& RunFrames)
{
	if (!ScatterHISMComponent) return;

	const bool bWantScatter =
		RoadType == ERoadType::RoughRoad &&
		ScatterSettings.bEnableScatter &&
		!ScatterSettings.ScatterMesh.IsNull() &&
		!bIsHolographicPreview;

	UStaticMesh* MeshAsset = bWantScatter ? ScatterSettings.ScatterMesh.LoadSynchronous() : nullptr;
	if (!MeshAsset)
	{
		ScatterHISMComponent->ClearInstances();
		return;
	}

	// Assigning a mesh drops existing instances, so it has to happen before the deliberate clear
	// rather than after it.
	if (ScatterHISMComponent->GetStaticMesh() != MeshAsset)
	{
		ScatterHISMComponent->SetStaticMesh(MeshAsset);
	}
	ScatterHISMComponent->ClearInstances();

	const double DistanceStep = 1000.0 / FMath::Max(0.1f, ScatterSettings.DensityPer10Meters);
	if (DistanceStep <= 0.0) return;

	// Keep debris clear of the running surface even if the margins were left narrower than the
	// road itself.
	const double HalfWidth = FMath::Max(50.0f, CrossSectionSettings.RoadWidth) * 0.5;
	const double MinMargin = FMath::Max(static_cast<double>(ScatterSettings.MinMarginOffset), HalfWidth * 1.15);
	const double MaxMargin = FMath::Max(MinMargin + 50.0, static_cast<double>(ScatterSettings.MaxMarginOffset));

	// A single stream across every run, so the seed reproduces the whole road rather than
	// repeating the same layout in each run.
	FRandomStream Rng(ScatterSettings.RandomSeed);

	for (const TArray<RoadMesh::FFrame>& Frames : RunFrames)
	{
		if (Frames.Num() < 2) continue;

		const double TotalLength = Frames.Last().DistanceAlong;
		if (TotalLength <= DistanceStep) continue;

		const int32 TotalSteps = FMath::FloorToInt(TotalLength / DistanceStep);
		int32 FrameCursor = 0;

		for (int32 Step = 0; Step < TotalSteps; ++Step)
		{
			const double TargetDistance = (Step * DistanceStep) + Rng.FRandRange(-DistanceStep * 0.4, DistanceStep * 0.4);
			if (TargetDistance < 0.0 || TargetDistance > TotalLength) continue;

			// Frames are ordered by distance and the target advances monotonically, so this walks
			// forward instead of searching from the start each time.
			while (FrameCursor + 1 < Frames.Num() && Frames[FrameCursor + 1].DistanceAlong < TargetDistance)
			{
				++FrameCursor;
			}
			const RoadMesh::FFrame& Frame = Frames[FrameCursor];

			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const double Margin = Rng.FRandRange(MinMargin, MaxMargin);
				FVector Location = Frame.Origin + Frame.Right * (Margin * Side);

				// Sitting instances on the ground rather than on the road's plane is what stops
				// rocks floating where the verge falls away.
				float GroundZ = 0.0f;
				if (SampleTerrainHeightAtXY(FVector2D(Location.X, Location.Y), GroundZ))
				{
					Location.Z = GroundZ;
				}
				Location.Z -= ScatterSettings.EmbedDepth;

				const FRotator Rotation(
					Rng.FRandRange(-15.0f, 15.0f),
					Rng.FRandRange(0.0f, 360.0f),
					Rng.FRandRange(-15.0f, 15.0f));

				const float Scale = Rng.FRandRange(ScatterSettings.MinScale, ScatterSettings.MaxScale);
				const FVector ScaleVector(Scale, Scale, Scale * Rng.FRandRange(0.8f, 1.2f));

				ScatterHISMComponent->AddInstance(FTransform(Rotation, Location, ScaleVector), /*bWorldSpace*/ true);
			}
		}
	}
}
