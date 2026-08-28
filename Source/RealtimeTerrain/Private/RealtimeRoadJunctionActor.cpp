// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeRoadJunctionActor.h"

#include "RoadMeshBuilder.h"
#include "RoadNetworkSolver.h"
#include "RoadNetwork.h"
#include "RealtimeRoadSplineActor.h"
#include "Components/SplineComponent.h"
#include "EngineUtils.h"

#include "Algo/Reverse.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Core/TerraDyneManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** A leg with its angle normalised and its width and local axes resolved. */
	struct FResolvedLeg
	{
		double AngleRad = 0.0;
		double HalfWidth = 0.0;
		double ArmLength = 0.0;
		FVector2D Dir = FVector2D(1.0, 0.0);
		/** Dir rotated 90 degrees counter-clockwise. */
		FVector2D Perp = FVector2D(0.0, 1.0);
	};

	FORCEINLINE double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** Beyond this gap there is no corner to round: the two legs are opposite or leave a reflex wedge. */
	constexpr double StraightGapThresholdDegrees = 178.0;
}

bool ARealtimeRoadJunctionActor::bAnySolveInProgress = false;

ARealtimeRoadJunctionActor::ARealtimeRoadJunctionActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	JunctionMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("JunctionMeshComponent"));
	JunctionMeshComponent->SetupAttachment(SceneRoot);
	JunctionMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	JunctionMeshComponent->SetGenerateOverlapEvents(false);
	JunctionMeshComponent->bEnableComplexCollision = true;

	// Matches ARealtimeRoadSplineActor's own defaults so a junction and the roads meeting it use the
	// same materials without per-actor setup. Still EditAnywhere, so any junction can override.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AsphaltFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Asphalt/M_Asphalt_Master_Inst_Intersection.M_Asphalt_Master_Inst_Intersection"));
	if (AsphaltFinder.Succeeded())
	{
		AsphaltMaterial = AsphaltFinder.Object;
	}
	else
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> FreewayAsphaltFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Asphalt/MI_FreewayAsphalt_Road.MI_FreewayAsphalt_Road"));
		if (FreewayAsphaltFinder.Succeeded()) AsphaltMaterial = FreewayAsphaltFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CurbFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Concrete/M_roadcurb_master_concrete_instance.M_roadcurb_master_concrete_instance"));
	if (CurbFinder.Succeeded()) CurbMaterial = CurbFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FootpathFinder(TEXT("/Game/UnrealDrive_CitySample/Materials/MI_Sidewalks/MI_Sidewalk.MI_Sidewalk"));
	if (FootpathFinder.Succeeded()) FootpathMaterial = FootpathFinder.Object;

	// Without this, a holographic draft (SetIsHolographicPreview -> ApplyMaterials) had nothing to
	// fall back to and silently kept its normal, opaque material -- a "preview" indistinguishable
	// from a real, already-committed junction. See ApplyMaterials's bIsHolographicPreview branch.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HolographicFinder(TEXT("/Game/Materials/M_RoadHolographic.M_RoadHolographic"));
	if (HolographicFinder.Succeeded()) HolographicMaterial = HolographicFinder.Object;

	SyncPresetLegs();
}

void ARealtimeRoadJunctionActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, JunctionType);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, RoadWidth);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, CornerFilletRadius);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, FilletSegments);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, CurbHeight);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, CurbWidth);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, FootpathWidth);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, FootpathHeight);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, RoadThickness);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, UVTilingScale);
	DOREPLIFETIME(ARealtimeRoadJunctionActor, Legs);
}

void ARealtimeRoadJunctionActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildJunction();
}

void ARealtimeRoadJunctionActor::BeginPlay()
{
	Super::BeginPlay();
	RebuildJunction();
}

#if WITH_EDITOR
void ARealtimeRoadJunctionActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedName = PropertyChangedEvent.GetPropertyName();

	// Switching preset used to do nothing, because leg regeneration only ran from the constructor
	// and when Legs happened to be empty.
	if (ChangedName == GET_MEMBER_NAME_CHECKED(ARealtimeRoadJunctionActor, JunctionType))
	{
		SyncPresetLegs();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ARealtimeRoadJunctionActor::OnRep_JunctionConfig()
{
	RebuildJunction();
}

void ARealtimeRoadJunctionActor::OnRep_Legs()
{
	RebuildJunction();
}

void ARealtimeRoadJunctionActor::SyncPresetLegs()
{
	auto Preset = [this](std::initializer_list<float> Angles)
	{
		Legs.Reset();
		for (float Angle : Angles)
		{
			FRoadJunctionLeg Leg;
			Leg.AngleDegrees = Angle;
			Leg.ArmLength = 500.0f;
			Leg.CustomRoadWidth = 0.0f;
			Legs.Add(Leg);
		}
	};

	switch (JunctionType)
	{
	case ERoadJunctionType::T_Junction:
		Preset({ 0.0f, 90.0f, 270.0f });
		break;

	case ERoadJunctionType::Cross_4Way:
		Preset({ 0.0f, 90.0f, 180.0f, 270.0f });
		break;

	case ERoadJunctionType::Y_Fork:
		Preset({ 0.0f, 135.0f, 225.0f });
		break;

	case ERoadJunctionType::Custom_N_Way:
	default:
		// Authored legs: leave them alone.
		break;
	}
}

void ARealtimeRoadJunctionActor::SetIsHolographicPreview(bool bHolographic)
{
	bIsHolographicPreview = bHolographic;

	if (bIsHolographicPreview)
	{
		JunctionMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (HasAuthority())
		{
			SetReplicates(false);
		}
	}
	else
	{
		JunctionMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		if (HasAuthority())
		{
			SetReplicates(true);
		}
	}

	ApplyMaterials();
	RebuildJunction();
}

void ARealtimeRoadJunctionActor::AddLegRealtime(float AngleDegrees, float ArmLength, float CustomRoadWidth)
{
	JunctionType = ERoadJunctionType::Custom_N_Way;
	Legs.Add({ AngleDegrees, ArmLength, CustomRoadWidth });
	RebuildJunction();
}

float ARealtimeRoadJunctionActor::GetLegWidth(int32 LegIndex) const
{
	if (!Legs.IsValidIndex(LegIndex))
	{
		return RoadWidth;
	}
	return (Legs[LegIndex].CustomRoadWidth > 0.0f) ? Legs[LegIndex].CustomRoadWidth : RoadWidth;
}

bool ARealtimeRoadJunctionActor::FindClosestLeg(const FVector& WorldLocation, int32& OutLegIndex, float& OutDistance) const
{
	OutLegIndex = INDEX_NONE;
	OutDistance = MAX_FLT;

	for (int32 i = 0; i < Legs.Num(); ++i)
	{
		FTransform LegTransform;
		if (GetLegConnectionTransform(i, LegTransform))
		{
			const float Distance = FVector::Dist(WorldLocation, LegTransform.GetLocation());
			if (Distance < OutDistance)
			{
				OutDistance = Distance;
				OutLegIndex = i;
			}
		}
	}

	return OutLegIndex != INDEX_NONE;
}

bool ARealtimeRoadJunctionActor::GetLegConnectionTransform(int32 LegIndex, FTransform& OutTransform) const
{
	if (!Legs.IsValidIndex(LegIndex))
	{
		return false;
	}

	const FRoadJunctionLeg& Leg = Legs[LegIndex];
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Leg.AngleDegrees));
	const FVector LocalDirection(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);

	const FTransform ActorTransform = GetActorTransform();
	const FVector WorldDirection = ActorTransform.TransformVector(LocalDirection).GetSafeNormal2D();
	const FVector ActorLocation = ActorTransform.GetLocation();

	// Each socket sits at its own arm's height. Using one shared plane here is what made a road
	// approaching a sloped intersection step up or down as it arrived.
	const double SocketZ = SolvedLegHeights.IsValidIndex(LegIndex)
		? static_cast<double>(SolvedLegHeights[LegIndex])
		: ConformedPlaneWorldZ;

	const FVector SocketLocation(
		ActorLocation.X + WorldDirection.X * Leg.ArmLength,
		ActorLocation.Y + WorldDirection.Y * Leg.ArmLength,
		SocketZ);

	OutTransform = FTransform(WorldDirection.Rotation(), SocketLocation);
	return true;
}

//--------------------------------------------------------------------------------------------
// Terrain
//--------------------------------------------------------------------------------------------

int32 ARealtimeRoadJunctionActor::GetRampCornerCount() const
{
	int32 Count = 0;
	for (const bool bIsRamp : SolvedIsRampCorner)
	{
		if (bIsRamp)
		{
			Count++;
		}
	}
	return Count;
}

int32 ARealtimeRoadJunctionActor::GetConnectorCount() const
{
	return SolvedConnectors.Num();
}

bool ARealtimeRoadJunctionActor::SampleTerrainHeightAtXY(const FVector2D& WorldXY, float& OutHeightZ) const
{
	return RoadMesh::SampleGroundHeight(this, CachedTerraDyneManager, WorldXY, OutHeightZ);
}

double ARealtimeRoadJunctionActor::ResolvePlaneWorldZ() const
{
	const FTransform ActorTransform = GetActorTransform();
	const FVector ActorLocation = ActorTransform.GetLocation();

	if (!bConformToTerrain)
	{
		return ActorLocation.Z;
	}

	// Averaging the centre with every arm end grades the intersection to the mean of the ground it
	// spans, which is what keeps all of its connecting roads reachable rather than favouring one.
	TArray<FVector2D> Samples;
	Samples.Reserve(Legs.Num() + 1);
	Samples.Add(FVector2D(ActorLocation.X, ActorLocation.Y));

	for (const FRoadJunctionLeg& Leg : Legs)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Leg.AngleDegrees));
		const FVector LocalDirection(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);
		const FVector WorldDirection = ActorTransform.TransformVector(LocalDirection).GetSafeNormal2D();
		Samples.Add(FVector2D(
			ActorLocation.X + WorldDirection.X * Leg.ArmLength,
			ActorLocation.Y + WorldDirection.Y * Leg.ArmLength));
	}

	double Sum = 0.0;
	int32 Count = 0;
	for (const FVector2D& Sample : Samples)
	{
		float GroundZ = 0.0f;
		if (SampleTerrainHeightAtXY(Sample, GroundZ))
		{
			Sum += GroundZ;
			++Count;
		}
	}

	if (Count == 0)
	{
		// Terrain is streamed out under every sample; the authored Z is the best guess available.
		return ActorLocation.Z;
	}

	return Sum / static_cast<double>(Count) + TerrainSurfaceOffset;
}

//--------------------------------------------------------------------------------------------
// Boundary construction
//--------------------------------------------------------------------------------------------

bool ARealtimeRoadJunctionActor::BuildBoundary(
	TArray<FVector>& OutBoundary,
	TArray<TArray<FVector>>& OutCornerChains,
	double& OutMaxBandWidth) const
{
	OutBoundary.Reset();
	OutCornerChains.Reset();
	OutMaxBandWidth = TNumericLimits<double>::Max();

	if (Legs.Num() < 2)
	{
		return false;
	}

	TArray<FResolvedLeg> Resolved;
	Resolved.Reserve(Legs.Num());
	for (const FRoadJunctionLeg& Leg : Legs)
	{
		FResolvedLeg Entry;
		Entry.AngleRad = FMath::DegreesToRadians(static_cast<double>(FRotator::ClampAxis(Leg.AngleDegrees)));
		Entry.HalfWidth = FMath::Max(100.0f, (Leg.CustomRoadWidth > 0.0f) ? Leg.CustomRoadWidth : RoadWidth) * 0.5;
		Entry.ArmLength = FMath::Max(100.0f, Leg.ArmLength);
		Entry.Dir = FVector2D(FMath::Cos(Entry.AngleRad), FMath::Sin(Entry.AngleRad));
		Entry.Perp = FVector2D(-Entry.Dir.Y, Entry.Dir.X);
		Resolved.Add(Entry);
	}

	// Sorting by angle is what makes "the next leg" mean "the next one round the circle", which the
	// corner construction depends on. It also makes the traversal counter-clockwise, which the
	// prism and sweep winding rules both assume.
	Resolved.Sort([](const FResolvedLeg& A, const FResolvedLeg& B) { return A.AngleRad < B.AngleRad; });

	const int32 NumLegs = Resolved.Num();
	const int32 Segments = FMath::Clamp(FilletSegments, 1, 32);
	const double StraightThreshold = FMath::DegreesToRadians(StraightGapThresholdDegrees);

	const FTransform ActorTransform = GetActorTransform();
	const FVector ActorLocation = ActorTransform.GetLocation();
	const double PlaneZ = ConformedPlaneWorldZ;

	// Local XY is rotated into world by the actor's yaw, but Z always comes from the junction
	// plane, so a tilted actor transform cannot tip the slab out of horizontal.
	auto ToWorld = [&](const FVector2D& Local) -> FVector
	{
		const FVector Rotated = ActorTransform.TransformVector(FVector(Local.X, Local.Y, 0.0));
		return FVector(ActorLocation.X + Rotated.X, ActorLocation.Y + Rotated.Y, PlaneZ);
	};

	OutCornerChains.SetNum(NumLegs);

	for (int32 i = 0; i < NumLegs; ++i)
	{
		const FResolvedLeg& Current = Resolved[i];
		const FResolvedLeg& Next = Resolved[(i + 1) % NumLegs];

		const FVector2D ArmClockwise = Current.Dir * Current.ArmLength - Current.Perp * Current.HalfWidth;
		const FVector2D ArmCounterClockwise = Current.Dir * Current.ArmLength + Current.Perp * Current.HalfWidth;

		OutBoundary.Add(ToWorld(ArmClockwise));
		OutBoundary.Add(ToWorld(ArmCounterClockwise));

		double Gap = Next.AngleRad - Current.AngleRad;
		while (Gap <= 0.0)
		{
			Gap += UE_DOUBLE_PI * 2.0;
		}

		const double HalfAngle = Gap * 0.5;
		TArray<FVector2D> Chain;

		// Fallback used whenever there is no well-formed corner to round: a single point on the
		// gap's bisector. For a T-junction's 180 degree gap this lands exactly on the straight
		// edge joining the two opposite legs; for a wider gap it bulges outward, which is what
		// stops the reflex case folding the boundary back through itself.
		auto AddBisectorPoint = [&]()
		{
			const double BisectorAngle = Current.AngleRad + HalfAngle;
			const double Radius = FMath::Max(Current.HalfWidth, Next.HalfWidth);
			Chain.Add(FVector2D(FMath::Cos(BisectorAngle), FMath::Sin(BisectorAngle)) * Radius);
		};

		if (Gap >= StraightThreshold)
		{
			AddBisectorPoint();
		}
		else
		{
			const FVector2D OriginA = Current.Perp * Current.HalfWidth;
			const FVector2D OriginB = -Next.Perp * Next.HalfWidth;
			const double Denominator = Cross2D(Current.Dir, Next.Dir);

			bool bResolvedCorner = false;

			if (FMath::Abs(Denominator) > 1.0e-6)
			{
				const FVector2D Delta = OriginB - OriginA;
				const double DistanceA = Cross2D(Delta, Next.Dir) / Denominator;
				const double DistanceB = Cross2D(Delta, Current.Dir) / Denominator;

				// A corner behind the centre, or past the arm ends, means the legs overlap rather
				// than meeting cleanly. The bisector fallback handles those.
				if (DistanceA > -Current.ArmLength && DistanceA < Current.ArmLength &&
					DistanceB > -Next.ArmLength && DistanceB < Next.ArmLength)
				{
					const FVector2D Corner = OriginA + Current.Dir * DistanceA;
					const double TanHalf = FMath::Tan(HalfAngle);
					const double SinHalf = FMath::Sin(HalfAngle);

					if (TanHalf > 1.0e-4 && SinHalf > 1.0e-4)
					{
						// The tangent points have to stay between the corner and the arm ends.
						const double RoomA = FMath::Max(0.0, (Current.ArmLength - DistanceA) * 0.85) * TanHalf;
						const double RoomB = FMath::Max(0.0, (Next.ArmLength - DistanceB) * 0.85) * TanHalf;
						const double Radius = FMath::Min3(static_cast<double>(CornerFilletRadius), RoomA, RoomB);

						if (Radius > 1.0)
						{
							const double TangentDistance = Radius / TanHalf;
							const FVector2D TangentA = Corner + Current.Dir * TangentDistance;
							const FVector2D TangentB = Corner + Next.Dir * TangentDistance;
							const FVector2D Bisector = (Current.Dir + Next.Dir).GetSafeNormal();
							const FVector2D Centre = Corner + Bisector * (Radius / SinHalf);

							double StartAngle = FMath::Atan2(TangentA.Y - Centre.Y, TangentA.X - Centre.X);
							const double EndAngle = FMath::Atan2(TangentB.Y - Centre.Y, TangentB.X - Centre.X);

							double Sweep = EndAngle - StartAngle;
							while (Sweep > UE_DOUBLE_PI) Sweep -= UE_DOUBLE_PI * 2.0;
							while (Sweep < -UE_DOUBLE_PI) Sweep += UE_DOUBLE_PI * 2.0;

							for (int32 k = 0; k <= Segments; ++k)
							{
								const double Angle = StartAngle + Sweep * (static_cast<double>(k) / Segments);
								Chain.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
							}

							// The roadside band offsets toward this arc's centre, so it cannot be
							// wider than the radius allows.
							OutMaxBandWidth = FMath::Min(OutMaxBandWidth, Radius / RoadMesh::BandRadiusSafetyFactor);
							bResolvedCorner = true;
						}
						else
						{
							// Fillet collapsed against a short arm: keep the sharp corner.
							Chain.Add(Corner);
							bResolvedCorner = true;
						}
					}
				}
			}

			if (!bResolvedCorner)
			{
				AddBisectorPoint();
			}
		}

		// The band runs from this leg's counter-clockwise arm corner, round the corner, to the next
		// leg's clockwise arm corner, so it meets the curbs of both connecting roads.
		TArray<FVector>& BandChain = OutCornerChains[i];
		BandChain.Reserve(Chain.Num() + 2);
		BandChain.Add(ToWorld(ArmCounterClockwise));

		for (const FVector2D& Point : Chain)
		{
			const FVector WorldPoint = ToWorld(Point);
			OutBoundary.Add(WorldPoint);
			BandChain.Add(WorldPoint);
		}

		const FVector2D NextArmClockwise = Next.Dir * Next.ArmLength - Next.Perp * Next.HalfWidth;
		BandChain.Add(ToWorld(NextArmClockwise));

		// Swept frames take Right from world up crossed with the tangent, which points inward along
		// a counter-clockwise path. Reversing the chain turns it outward, which is the direction the
		// roadside section is authored in.
		Algo::Reverse(BandChain);
	}

	return OutBoundary.Num() >= 3;
}

//--------------------------------------------------------------------------------------------
// Mesh generation
//--------------------------------------------------------------------------------------------

void ARealtimeRoadJunctionActor::ApplyMaterials()
{
	if (!JunctionMeshComponent) return;

	TArray<UMaterialInterface*> MaterialSet;
	MaterialSet.SetNum(RoadMesh::Slots::Count);

	if (bIsHolographicPreview && HolographicMaterial)
	{
		for (UMaterialInterface*& Slot : MaterialSet)
		{
			Slot = HolographicMaterial;
		}
	}
	else
	{
		MaterialSet[RoadMesh::Slots::Surface] = AsphaltMaterial;
		MaterialSet[RoadMesh::Slots::Curb] = CurbMaterial;
		MaterialSet[RoadMesh::Slots::Footpath] = FootpathMaterial;
		MaterialSet[RoadMesh::Slots::MarkingWhite] = AsphaltMaterial;
		MaterialSet[RoadMesh::Slots::MarkingYellow] = AsphaltMaterial;
	}

	JunctionMeshComponent->ConfigureMaterialSet(MaterialSet);
}

bool ARealtimeRoadJunctionActor::SolveIntersection()
{
	// Re-entrancy: the cut writes below rebuild roads, and a rebuilding road asks its junctions to
	// re-solve. Without this guard that is an infinite mutual recursion.
	if (bAnySolveInProgress)
	{
		return false;
	}
	TGuardValue<bool> SolveGuard(bAnySolveInProgress, true);

	bLastSolveWasOrphaned = false;

	SolvedBoundary.Reset();
	SolvedRoadsideChains.Reset();
	SolvedCornerRadii.Reset();
	SolvedIsRampCorner.Reset();
	SolvedGoreApex.Reset();
	SolvedConnectors.Reset();

	UWorld* World = GetWorld();
	if (!World) return false;

	const FVector Centre = GetActorLocation();
	const FVector2D CentreXY(Centre.X, Centre.Y);
	const double Radius = FMath::Max(100.0f, BoundsRadius);

	// How far past the node each arm is sampled. The solver needs edge beyond the corner to find
	// collisions and fit the return arc into, so this reaches well outside the bounds itself.
	const double SampleReach = Radius * 2.5;
	const double SampleStep = FMath::Max(100.0, Radius * 0.1);

	struct FArmSource
	{
		ARealtimeRoadSplineActor* Road = nullptr;
		double NodeDistance = 0.0;
		bool bForward = false;
	};
	TArray<FArmSource> Sources;
	TArray<RoadNetworkSolver::FArm> Arms;

	for (TActorIterator<ARealtimeRoadSplineActor> It(World); It; ++It)
	{
		ARealtimeRoadSplineActor* Road = *It;
		if (!Road || Road->bIsHolographicPreview || !Road->SplineComponent) continue;

		USplineComponent* Spline = Road->SplineComponent;
		if (Spline->GetNumberOfSplinePoints() < 2) continue;

		const double Length = Spline->GetSplineLength();

		// Plan-space, so a height difference between this node and the road cannot send the search
		// to the wrong stretch of spline.
		double NodeDistance = 0.0;
		if (!RoadNetwork::FindDistanceAlongRoad2D(Road, CentreXY, NodeDistance))
		{
			continue;
		}
		NodeDistance = FMath::Clamp(NodeDistance, 0.0, Length);

		const FVector Closest = Spline->GetLocationAtDistanceAlongSpline(
			NodeDistance, ESplineCoordinateSpace::World);
		if (FVector::Dist2D(Closest, Centre) > Radius) continue;

		const double HalfWidth =
			FMath::Max(50.0f, Road->CrossSectionSettings.RoadWidth) * 0.5
			+ (Road->CrossSectionSettings.bHasCurbs ? Road->CrossSectionSettings.CurbWidth : 0.0f)
			+ (Road->CrossSectionSettings.bHasFootpaths ? Road->CrossSectionSettings.FootpathWidth : 0.0f);

		// A road passing straight through contributes two arms, one each way. A road that ends
		// inside the bounds contributes only the side that still has length to it.
		const double EndTolerance = HalfWidth;

		auto BuildArm = [&](bool bForward)
		{
			const double Available = bForward ? (Length - NodeDistance) : NodeDistance;
			if (Available <= EndTolerance) return;

			RoadNetworkSolver::FArm Arm;
			Arm.HalfWidth = HalfWidth;
			Arm.DrivingHalfWidth = Road->GetDrivingSurfaceHalfWidth();
			Arm.DrivingLaneCount = Road->GetDrivingLaneCount();
			Arm.UserData = Sources.Num();

			// Every arm has to start at the node itself.
			//
			// Sampling straight from each road's own closest point instead left the arms radiating
			// from slightly different origins, and the solver's collisions are between the sides of
			// arms assumed to share one. The corners then missed each other entirely: two arms fell
			// back to the bare half-width stand-off while the others overshot to nearly three times
			// it, which is what tore the intersection into sprawling flanges.
			Arm.Centreline.Add(CentreXY);

			// Start a full step out, never at the node itself. The road's closest point sits a few
			// centimetres from the node, so sampling it would make the arm's first segment a
			// near-zero step whose direction is noise — and the solver reads the arm's heading from
			// exactly that segment. Both directions of a road then reported the same angle.
			const double Reach = FMath::Min(SampleReach, Available);
			for (double T = SampleStep; T <= Reach + UE_KINDA_SMALL_NUMBER; T += SampleStep)
			{
				const double D = bForward ? (NodeDistance + T) : (NodeDistance - T);
				const FVector P = Spline->GetLocationAtDistanceAlongSpline(
					FMath::Clamp(D, 0.0, Length), ESplineCoordinateSpace::World);

				const FVector2D Sample(P.X, P.Y);
				if (FVector2D::DistSquared(Sample, Arm.Centreline.Last()) > 1.0)
				{
					Arm.Centreline.Add(Sample);
				}
			}

			if (Arm.Centreline.Num() < 2) return;

			Sources.Add({ Road, NodeDistance, bForward });
			Arms.Add(MoveTemp(Arm));
		};

		BuildArm(false);
		BuildArm(true);
	}

	if (Arms.Num() < 2)
	{
		bLastSolveWasOrphaned = true;

		// Fewer than two roads means there's nothing left to solve an intersection between -- most
		// often because a connected road (or enough of them) just got deleted out from under this
		// junction. If exactly one road is still nearby, it's now stuck pinned to a junction that
		// no longer has anything to be a junction between, so un-pin it back to a normal, uncut
		// road rather than leaving it artificially cut short at this node. RebuildJunction (the
		// caller) is the one that actually removes this junction actor -- see bLastSolveWasOrphaned
		// there -- since a freshly-placed junction with no roads near it yet must NOT be destroyed
		// the moment it's created, only one that had already solved successfully before and has now
		// lost its connections.
		if (Sources.Num() == 1 && Sources[0].Road)
		{
			ARealtimeRoadSplineActor* Survivor = Sources[0].Road;
			Survivor->RemoveJunctionCutsFor(this);
			if (Survivor->StartJunction == this)
			{
				Survivor->StartJunction = nullptr;
				Survivor->StartJunctionLegIndex = INDEX_NONE;
			}
			if (Survivor->EndJunction == this)
			{
				Survivor->EndJunction = nullptr;
				Survivor->EndJunctionLegIndex = INDEX_NONE;
			}
			Survivor->RebuildRoad();
		}
		return false;
	}

	RoadNetworkSolver::FNodeOptions Options;
	Options.CornerRadius = FMath::Max(0.0f, CornerReturnRadius);
	Options.ArcSegments = FMath::Clamp(CornerArcSegments, 1, 32);

	const RoadNetworkSolver::FNodeSolution Solution =
		RoadNetworkSolver::SolveNode(CentreXY, Arms, Options);

	if (!Solution.bValid)
	{
		return false;
	}

	// Height per arm, taken where that road meets the node. The junction is no longer one plane:
	// it takes its height from the roads themselves, so it follows the ground they follow.
	TArray<float> ArmHeights;
	ArmHeights.SetNumZeroed(Arms.Num());

	for (int32 i = 0; i < Arms.Num(); ++i)
	{
		const RoadNetworkSolver::FArm& Arm = Arms[i];
		const FVector2D Tip = RoadNetworkSolver::PointAtDistance(Arm.Centreline, Arm.TrimDistance);

		float GroundZ = 0.0f;
		if (!SampleTerrainHeightAtXY(Tip, GroundZ))
		{
			GroundZ = static_cast<float>(ConformedPlaneWorldZ);
		}

		float Lift = TerrainSurfaceOffset;
		if (Sources.IsValidIndex(Arm.UserData) && Sources[Arm.UserData].Road)
		{
			Lift = Sources[Arm.UserData].Road->TerrainSurfaceOffset;
		}
		ArmHeights[i] = GroundZ + Lift;
	}

	auto HeightAt = [&ArmHeights](const RoadNetworkSolver::FBoundaryVertex& Vertex) -> float
	{
		if (ArmHeights.Num() == 0) return 0.0f;
		const int32 A = FMath::Clamp(Vertex.Arm, 0, ArmHeights.Num() - 1);
		const int32 B = (A + 1) % ArmHeights.Num();
		return FMath::Lerp(ArmHeights[A], ArmHeights[B], FMath::Clamp(Vertex.ArcAlpha, 0.0f, 1.0f));
	};

	SolvedBoundary.Reset(Solution.Boundary.Num());
	for (const RoadNetworkSolver::FBoundaryVertex& Vertex : Solution.Boundary)
	{
		SolvedBoundary.Add(FVector(Vertex.Position.X, Vertex.Position.Y, HeightAt(Vertex)));
	}

	SolvedRoadsideChains.Reset(Solution.RoadsideChains.Num());
	for (const TArray<RoadNetworkSolver::FBoundaryVertex>& Chain : Solution.RoadsideChains)
	{
		TArray<FVector>& Out = SolvedRoadsideChains.AddDefaulted_GetRef();
		Out.Reserve(Chain.Num());
		for (const RoadNetworkSolver::FBoundaryVertex& Vertex : Chain)
		{
			Out.Add(FVector(Vertex.Position.X, Vertex.Position.Y, HeightAt(Vertex)));
		}
	}

	SolvedCornerRadii = Solution.CornerRadii;
	SolvedLegHeights = ArmHeights;

	SolvedIsRampCorner = Solution.IsRampCorner;
	SolvedGoreApex.Init(FVector::ZeroVector, Arms.Num());
	for (int32 i = 0; i < Arms.Num(); ++i)
	{
		if (!SolvedIsRampCorner.IsValidIndex(i) || !SolvedIsRampCorner[i])
		{
			continue;
		}

		const int32 Next = (i + 1) % Arms.Num();
		FVector2D ApexXY;
		if (RoadNetworkSolver::FindGoreApex(Arms[i], Arms[Next], ApexXY))
		{
			// Blended, not either arm's own height outright: the gore sits between the two roads,
			// not clearly on one or the other, the same reasoning HeightAt already applies to every
			// other boundary vertex around a corner.
			const float ApexHeight = FMath::Lerp(ArmHeights[i], ArmHeights[Next], 0.5f);
			SolvedGoreApex[i] = FVector(ApexXY.X, ApexXY.Y, ApexHeight);
		}
		else
		{
			// The two arms' driving edges diverge outright rather than converge -- an ordinary
			// non-ramp-shaped fork with no real gore point, even though the angle/lane-count test
			// alone said "ramp". Treat it as not-a-gore rather than fabricating a fill.
			SolvedIsRampCorner[i] = false;
		}
	}

	// Per-lane connectors across every corner, ramp or not -- a through-connection at an ordinary
	// crossing needs lane continuity just as much as a ramp merge does.
	constexpr int32 ConnectorSegments = 8;
	SolvedConnectors.Reset();
	for (int32 i = 0; i < Arms.Num(); ++i)
	{
		const int32 Next = (i + 1) % Arms.Num();
		const RoadNetworkSolver::FArm& ArmA = Arms[i];
		const RoadNetworkSolver::FArm& ArmB = Arms[Next];
		if (ArmA.DrivingLaneCount <= 0 || ArmB.DrivingLaneCount <= 0)
		{
			continue;
		}

		TArray<int32> LaneMap;
		RoadNetworkSolver::MapLaneConnections(ArmA.DrivingLaneCount, ArmB.DrivingLaneCount, LaneMap);

		const double LaneWidthA = (ArmA.DrivingHalfWidth * 2.0) / ArmA.DrivingLaneCount;
		const double LaneWidthB = (ArmB.DrivingHalfWidth * 2.0) / ArmB.DrivingLaneCount;

		for (int32 LaneIndexA = 0; LaneIndexA < LaneMap.Num(); ++LaneIndexA)
		{
			const int32 LaneIndexB = LaneMap[LaneIndexA];
			if (LaneIndexB == INDEX_NONE)
			{
				continue;
			}

			TArray<FVector2D> Centreline2D;
			if (!RoadNetworkSolver::BuildLaneConnectorCentreline(ArmA, LaneIndexA, ArmB, LaneIndexB, ConnectorSegments, Centreline2D))
			{
				continue;
			}

			FSolvedLaneConnector& Connector = SolvedConnectors.AddDefaulted_GetRef();
			Connector.Width = (LaneWidthA + LaneWidthB) * 0.5;
			Connector.Centreline.Reserve(Centreline2D.Num());
			for (int32 k = 0; k < Centreline2D.Num(); ++k)
			{
				// Height blended along the connector's own parameter, not fixed at either end's
				// height outright, so a connector between two roads at different heights slopes
				// smoothly across the corner instead of stepping at one end.
				const float Alpha = static_cast<float>(k) / static_cast<float>(Centreline2D.Num() - 1);
				// The same small lift RoadBuilder's own TerrainSurfaceOffset pattern uses elsewhere
				// in this codebase: the connector's height comes from the identical arm-height
				// blend the slab's own cap vertices already use, so without a lift the two would
				// sit exactly coincident and z-fight rather than the connector reading as the
				// slightly-raised, distinct driving strip it's meant to be.
				constexpr float ConnectorLift = 1.0f;
				const float Height = FMath::Lerp(ArmHeights[i], ArmHeights[Next], Alpha) + ConnectorLift;
				Connector.Centreline.Add(FVector(Centreline2D[k].X, Centreline2D[k].Y, Height));
			}
		}
	}

	// Rebuild Legs from the solved arms. Roads still pin themselves to leg sockets, so the legs
	// have to agree with what the solver decided rather than with whatever was authored before.
	JunctionType = ERoadJunctionType::Custom_N_Way;
	Legs.Reset(Arms.Num());

	struct FRoadCut
	{
		ARealtimeRoadSplineActor* Road = nullptr;
		double NodeDistance = 0.0;
		double BackTrim = -1.0;
		double ForwardTrim = -1.0;
		int32 BackLeg = INDEX_NONE;
		int32 ForwardLeg = INDEX_NONE;
	};
	TMap<ARealtimeRoadSplineActor*, FRoadCut> Cuts;

	for (const RoadNetworkSolver::FArm& Arm : Arms)
	{
		if (!Sources.IsValidIndex(Arm.UserData)) continue;
		const FArmSource& Source = Sources[Arm.UserData];

		FRoadJunctionLeg Leg;
		Leg.AngleDegrees = FRotator::ClampAxis(FMath::RadiansToDegrees(Arm.AngleRad));
		Leg.ArmLength = static_cast<float>(FMath::Max(50.0, Arm.TrimDistance));
		Leg.CustomRoadWidth = 0.0f;
		const int32 LegIndex = Legs.Add(Leg);

		FRoadCut& Cut = Cuts.FindOrAdd(Source.Road);
		Cut.Road = Source.Road;
		Cut.NodeDistance = Source.NodeDistance;
		if (Source.bForward)
		{
			Cut.ForwardTrim = Arm.TrimDistance;
			Cut.ForwardLeg = LegIndex;
		}
		else
		{
			Cut.BackTrim = Arm.TrimDistance;
			Cut.BackLeg = LegIndex;
		}
	}

	// Sockets have to carry the conformed plane height before any road pins itself to one.
	ConformedPlaneWorldZ = ResolvePlaneWorldZ();

	for (const TPair<ARealtimeRoadSplineActor*, FRoadCut>& Pair : Cuts)
	{
		const FRoadCut& Cut = Pair.Value;
		if (!Cut.Road) continue;

		if (Cut.BackTrim >= 0.0 && Cut.ForwardTrim >= 0.0)
		{
			FRoadJunctionCut RoadCut;
			RoadCut.Junction = this;
			RoadCut.EntryDistance = static_cast<float>(Cut.NodeDistance - Cut.BackTrim);
			RoadCut.ExitDistance = static_cast<float>(Cut.NodeDistance + Cut.ForwardTrim);
			RoadCut.EntryLegIndex = Cut.BackLeg;
			RoadCut.ExitLegIndex = Cut.ForwardLeg;
			Cut.Road->AddJunctionCut(RoadCut);
		}
		else if (Cut.ForwardTrim >= 0.0)
		{
			// Only the far side survives, so the junction terminates this road's start.
			Cut.Road->RemoveJunctionCutsFor(this);
			Cut.Road->StartJunction = this;
			Cut.Road->StartJunctionLegIndex = Cut.ForwardLeg;
			Cut.Road->RebuildRoad();
		}
		else if (Cut.BackTrim >= 0.0)
		{
			Cut.Road->RemoveJunctionCutsFor(this);
			Cut.Road->EndJunction = this;
			Cut.Road->EndJunctionLegIndex = Cut.BackLeg;
			Cut.Road->RebuildRoad();
		}
	}

	return true;
}

void ARealtimeRoadJunctionActor::RebuildJunction()
{
	if (!JunctionMeshComponent) return;

	// Presets own their legs; only Custom_N_Way is expected to arrive with an authored list.
	if (JunctionType != ERoadJunctionType::Custom_N_Way && Legs.Num() == 0)
	{
		SyncPresetLegs();
	}

	ApplyMaterials();

	UDynamicMesh* DynamicMesh = JunctionMeshComponent->GetDynamicMesh();
	if (!DynamicMesh) return;

	ConformedPlaneWorldZ = ResolvePlaneWorldZ();

	TArray<FVector> Boundary;
	TArray<TArray<FVector>> CornerChains;

	// Room available for a curb/footpath band at each entry of CornerChains, aligned 1:1 with it.
	// Kept per-corner rather than collapsed to one junction-wide minimum: a junction with one tight
	// corner (several arms clustered a few degrees apart, say) used to lose curbs and footpaths
	// everywhere on it, even at corners with plenty of room to spare, because a single global scale
	// was clamped against whichever corner was tightest. Only the corner actually short on room
	// should lose its band.
	TArray<double> CornerBandRadii;
	double MaxBandWidth = 0.0;

	// Prefer a surface solved from the roads actually passing through the bounds. Authored legs
	// remain the fallback for a junction placed with no roads near it yet.
	bool bHaveGeometry = false;

	if (bSolveFromNearbyRoads && SolveIntersection() && SolvedBoundary.Num() >= 3)
	{
		Boundary = SolvedBoundary;

		CornerChains.Reset(SolvedRoadsideChains.Num());
		CornerBandRadii.Reset(SolvedRoadsideChains.Num());
		for (int32 ChainIndex = 0; ChainIndex < SolvedRoadsideChains.Num(); ++ChainIndex)
		{
			TArray<FVector>& Out = CornerChains.AddDefaulted_GetRef();
			Out = SolvedRoadsideChains[ChainIndex];

			// Swept frames take Right from world up crossed with the tangent, which points inward
			// along a counter-clockwise path; reversing turns it outward.
			Algo::Reverse(Out);

			// The solver clamps each corner's own fillet radius against the geometry available
			// there, but that is not the same as clamping the roadside band -- a tight/acute corner
			// can still resolve a small radius, and a curb+footpath band wider than that radius
			// folds over itself exactly like BuildBoundary's fallback path already guards against
			// below.
			CornerBandRadii.Add(SolvedCornerRadii.IsValidIndex(ChainIndex)
				? SolvedCornerRadii[ChainIndex]
				: TNumericLimits<double>::Max());
		}

		// Still used by BuildBoundary's fallback path below, which has no per-corner radii of its
		// own to offer.
		MaxBandWidth = TNumericLimits<double>::Max();
		for (const double CornerRadius : SolvedCornerRadii)
		{
			MaxBandWidth = FMath::Min(MaxBandWidth, CornerRadius / RoadMesh::BandRadiusSafetyFactor);
		}
		bHaveGeometry = true;
	}

	// This junction had solved successfully from real roads at some point before (JunctionType
	// only ever becomes Custom_N_Way as a side effect of that), and this call found fewer than two
	// of them left connected -- it is now an orphan, not a placeholder waiting for roads to be
	// drawn to it (that case never sets bLastSolveWasOrphaned to begin with, since Custom_N_Way
	// stays whatever preset it was spawned with until a solve actually succeeds once). Falling
	// through to BuildBoundary's authored-Legs fallback below would just keep showing whatever
	// shape the last successful solve left behind, disconnected from any real road.
	if (!bHaveGeometry && bLastSolveWasOrphaned && JunctionType == ERoadJunctionType::Custom_N_Way)
	{
		Destroy();
		return;
	}

	if (!bHaveGeometry && !BuildBoundary(Boundary, CornerChains, MaxBandWidth))
	{
		DynamicMesh->Reset();
		JunctionMeshComponent->NotifyMeshUpdated();
		return;
	}

	const float UVScale = FMath::Max(1.0e-5f, UVTilingScale);
	const double Thickness = FMath::Max(2.0f, RoadThickness);
	const double RequestedBand = FMath::Max(0.0f, CurbWidth) + FMath::Max(0.0f, FootpathWidth);

	const FTransform WorldToLocal = JunctionMeshComponent->GetComponentTransform().Inverse();

	DynamicMesh->Reset();
	DynamicMesh->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		RoadMesh::AppendPrism(
			Mesh, Boundary, Thickness,
			RoadMesh::Slots::Surface, RoadMesh::Slots::Flank,
			WorldToLocal, UVScale);

		// Gore islands at ramp/fork corners. Independent of whether this junction has curbs or
		// footpaths configured at all -- a gore reads as a raised island regardless -- and only
		// meaningful on the solved-from-real-roads path: the authored-Legs fallback below has no
		// ramp classification of its own, and SolvedIsRampCorner/SolvedGoreApex would otherwise be
		// stale leftovers from whatever this junction last solved successfully from real roads.
		if (bHaveGeometry)
		{
			for (int32 ChainIndex = 0; ChainIndex < CornerChains.Num(); ++ChainIndex)
			{
				if (!SolvedIsRampCorner.IsValidIndex(ChainIndex) || !SolvedIsRampCorner[ChainIndex])
				{
					continue;
				}
				RoadMesh::AppendGoreFill(
					Mesh, SolvedGoreApex[ChainIndex], CornerChains[ChainIndex],
					RoadMesh::Slots::Curb, WorldToLocal, UVScale);
			}

			// Per-lane connectors: a flat single-lane strip swept along each solved connector
			// centreline, giving real lane-to-lane continuity across the junction instead of an
			// undifferentiated slab -- the piece that makes this a routed intersection rather than
			// just a correctly-shaped one.
			RoadMesh::FSweepOptions ConnectorOptions;
			ConnectorOptions.UVScale = UVScale;
			ConnectorOptions.bSectionIsClosed = false;
			ConnectorOptions.bCapStart = false;
			ConnectorOptions.bCapEnd = false;

			for (const FSolvedLaneConnector& Connector : SolvedConnectors)
			{
				if (Connector.Centreline.Num() < 2)
				{
					continue;
				}

				RoadMesh::FSection LaneSection;
				const double HalfWidth = FMath::Max(1.0, Connector.Width) * 0.5;
				LaneSection.Emplace(-HalfWidth, 0.0, RoadMesh::Slots::Surface);
				LaneSection.Emplace(HalfWidth, 0.0, RoadMesh::Slots::Surface);

				TArray<RoadMesh::FFrame> ConnectorFrames;
				if (RoadMesh::BuildFramesFromPolyline(Connector.Centreline, ConnectorFrames))
				{
					RoadMesh::SweepSection(Mesh, LaneSection, ConnectorFrames, WorldToLocal, ConnectorOptions);
				}
			}
		}

		if (RequestedBand <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}

		RoadMesh::FSweepOptions BandOptions;
		BandOptions.UVScale = UVScale;
		// An open strip: the junction slab's own skirt already occupies the plane where this
		// band's inner wall would sit, and two coincident faces would z-fight.
		BandOptions.bSectionIsClosed = false;
		BandOptions.bCapStart = false;
		BandOptions.bCapEnd = false;

		for (int32 ChainIndex = 0; ChainIndex < CornerChains.Num(); ++ChainIndex)
		{
			// BuildBoundary's fallback path supplies one shared MaxBandWidth for every chain
			// (already * BandRadiusSafetyFactor applied); the solved path has a real per-corner
			// radius to divide back down instead.
			const double CornerRadius = CornerBandRadii.IsValidIndex(ChainIndex)
				? CornerBandRadii[ChainIndex]
				: MaxBandWidth * RoadMesh::BandRadiusSafetyFactor;
			const double AvailableForBand = CornerRadius / RoadMesh::BandRadiusSafetyFactor;
			const double ChainBandScale = FMath::Clamp(AvailableForBand / RequestedBand, 0.0, 1.0);

			if (ChainBandScale <= 0.05)
			{
				continue;
			}

			RoadMesh::FSection ChainBandSection;
			RoadMesh::BuildRoadsideSection(
				CurbWidth * ChainBandScale,
				CurbHeight,
				FootpathWidth * ChainBandScale,
				FootpathHeight,
				Thickness,
				ChainBandSection);

			if (ChainBandSection.Num() < 2)
			{
				continue;
			}

			TArray<RoadMesh::FFrame> Frames;
			if (RoadMesh::BuildFramesFromPolyline(CornerChains[ChainIndex], Frames))
			{
				RoadMesh::SweepSection(Mesh, ChainBandSection, Frames, WorldToLocal, BandOptions);
			}
		}
	});

	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
		DynamicMesh,
		FGeometryScriptSplitNormalsOptions(),
		FGeometryScriptCalculateNormalsOptions());

	JunctionMeshComponent->NotifyMeshUpdated();
}
