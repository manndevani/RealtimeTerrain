// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadNetwork.h"

#include "RealtimeRoadSplineActor.h"
#include "RealtimeRoadJunctionActor.h"

#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#include "RoadMeshBuilder.h"
#include "Engine/World.h"

namespace RoadNetwork
{

double ComputeArmLength(const TArray<float>& HeadingsDegrees, double MaxHalfWidth)
{
	const double Floor = MaxHalfWidth * 1.5;
	const double Ceiling = MaxHalfWidth * 8.0;

	if (HeadingsDegrees.Num() < 2)
	{
		return Floor;
	}

	TArray<double> Radians;
	Radians.Reserve(HeadingsDegrees.Num());
	for (float Heading : HeadingsDegrees)
	{
		Radians.Add(FMath::DegreesToRadians(static_cast<double>(FRotator::ClampAxis(Heading))));
	}
	Radians.Sort();

	double MinGap = UE_DOUBLE_PI * 2.0;
	for (int32 i = 0; i < Radians.Num(); ++i)
	{
		double Gap = Radians[(i + 1) % Radians.Num()] - Radians[i];
		while (Gap <= 0.0)
		{
			Gap += UE_DOUBLE_PI * 2.0;
		}
		MinGap = FMath::Min(MinGap, Gap);
	}

	const double TanHalf = FMath::Tan(MinGap * 0.5);

	// A vanishing gap sends the corner off to infinity; clamp rather than chase it.
	const double CornerDistance = (TanHalf > 0.05) ? (MaxHalfWidth / TanHalf) : Ceiling;

	return FMath::Clamp(CornerDistance * 1.5, Floor, Ceiling);
}

bool FindDistanceAlongRoad(const ARealtimeRoadSplineActor* Road, const FVector& WorldLocation, double& OutDistance)
{
	OutDistance = 0.0;
	if (!Road || !Road->SplineComponent) return false;

	const USplineComponent* Spline = Road->SplineComponent;
	if (Spline->GetNumberOfSplinePoints() < 2) return false;

	const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	OutDistance = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);
	return true;
}

bool FindDistanceAlongRoad2D(const ARealtimeRoadSplineActor* Road, const FVector2D& WorldXY, double& OutDistance)
{
	OutDistance = 0.0;
	if (!Road || !Road->SplineComponent) return false;

	const USplineComponent* Spline = Road->SplineComponent;
	if (Spline->GetNumberOfSplinePoints() < 2) return false;

	const double Length = Spline->GetSplineLength();
	if (Length <= UE_KINDA_SMALL_NUMBER) return false;

	// Coarse sweep then a local refine: cheaper than a fine sweep and accurate enough, since the
	// result only has to land on the right stretch of road.
	const int32 Coarse = FMath::Clamp(FMath::CeilToInt(Length / 100.0), 8, 4096);
	double Best = 0.0;
	double BestDistSq = TNumericLimits<double>::Max();

	for (int32 i = 0; i <= Coarse; ++i)
	{
		const double D = Length * i / static_cast<double>(Coarse);
		const FVector P = Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		const double DistSq = FVector2D::DistSquared(FVector2D(P.X, P.Y), WorldXY);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = D;
		}
	}

	const double Window = Length / static_cast<double>(Coarse);
	for (int32 i = -8; i <= 8; ++i)
	{
		const double D = FMath::Clamp(Best + Window * i / 8.0, 0.0, Length);
		const FVector P = Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		const double DistSq = FVector2D::DistSquared(FVector2D(P.X, P.Y), WorldXY);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = D;
		}
	}

	OutDistance = Best;
	return true;
}

ARealtimeRoadJunctionActor* CreateJunctionOnRoad(
	ARealtimeRoadSplineActor* Road,
	double DistanceAlong,
	const TArray<float>& ExtraHeadingsDegrees,
	TArray<int32>& OutExtraLegIndices)
{
	OutExtraLegIndices.Reset();

	if (!Road || !Road->SplineComponent) return nullptr;

	USplineComponent* Spline = Road->SplineComponent;
	const double SplineLength = Spline->GetSplineLength();
	if (SplineLength < 100.0) return nullptr;

	UWorld* World = Road->GetWorld();
	if (!World) return nullptr;

	const double Distance = FMath::Clamp(DistanceAlong, 0.0, SplineLength);
	const FVector Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

	// Direction comes from a finite difference of positions, not from GetTangentAtDistanceAlongSpline.
	// That call returns a near-zero magnitude on these routes — measured at 4e-6 on a straight
	// 6000 cm road — so normalising it yields nothing and every junction was silently rejected.
	// The mesh builder already derives its frames the same way for the same reason.
	const double Probe = FMath::Clamp(SplineLength * 0.01, 10.0, 200.0);
	const FVector Ahead = Spline->GetLocationAtDistanceAlongSpline(
		FMath::Min(Distance + Probe, SplineLength), ESplineCoordinateSpace::World);
	const FVector Behind = Spline->GetLocationAtDistanceAlongSpline(
		FMath::Max(Distance - Probe, 0.0), ESplineCoordinateSpace::World);

	const FVector Tangent = (Ahead - Behind).GetSafeNormal2D();
	if (Tangent.IsNearlyZero()) return nullptr;

	const float ForwardHeading = FRotator::ClampAxis(FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X)));
	const float BackwardHeading = FRotator::ClampAxis(ForwardHeading + 180.0f);

	const double HalfWidth = FMath::Max(100.0f, Road->CrossSectionSettings.RoadWidth) * 0.5;

	// Close enough to an end and there is no stretch left on that side to give a leg to; the
	// junction becomes that end's terminus instead of a cut through the middle.
	const double EndTolerance = HalfWidth * 2.0;
	const bool bHasBehind = Distance > EndTolerance;
	const bool bHasAhead = Distance < SplineLength - EndTolerance;

	TArray<float> Headings;
	int32 BehindLeg = INDEX_NONE;
	int32 AheadLeg = INDEX_NONE;

	if (bHasBehind)
	{
		BehindLeg = Headings.Add(BackwardHeading);
	}
	if (bHasAhead)
	{
		AheadLeg = Headings.Add(ForwardHeading);
	}
	for (float Heading : ExtraHeadingsDegrees)
	{
		OutExtraLegIndices.Add(Headings.Add(FRotator::ClampAxis(Heading)));
	}

	if (Headings.Num() < 2)
	{
		OutExtraLegIndices.Reset();
		return nullptr;
	}

	const double ArmLength = ComputeArmLength(Headings, HalfWidth);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARealtimeRoadJunctionActor* Junction = World->SpawnActor<ARealtimeRoadJunctionActor>(
		ARealtimeRoadJunctionActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Junction)
	{
		OutExtraLegIndices.Reset();
		return nullptr;
	}

	// Match the road so the two line up in section as well as in plan.
	const FRoadCrossSectionSettings& Section = Road->CrossSectionSettings;
	Junction->JunctionType = ERoadJunctionType::Custom_N_Way;
	Junction->RoadWidth = Section.RoadWidth;
	Junction->CurbWidth = Section.bHasCurbs ? Section.CurbWidth : 0.0f;
	Junction->CurbHeight = Section.bHasCurbs ? Section.CurbHeight : 0.0f;
	Junction->FootpathWidth = Section.bHasFootpaths ? Section.FootpathWidth : 0.0f;
	Junction->FootpathHeight = Section.bHasFootpaths ? Section.FootpathHeight : 0.0f;
	Junction->RoadThickness = Section.RoadThickness;
	Junction->UVTilingScale = Section.UVTilingScale;
	Junction->bConformToTerrain = Road->bConformToTerrainInRealtime;
	Junction->TerrainSurfaceOffset = Road->TerrainSurfaceOffset;

	Junction->Legs.Reset(Headings.Num());
	for (float Heading : Headings)
	{
		FRoadJunctionLeg Leg;
		Leg.AngleDegrees = Heading;
		Leg.ArmLength = static_cast<float>(ArmLength);
		Leg.CustomRoadWidth = 0.0f;
		Junction->Legs.Add(Leg);
	}

	// Build now so the leg sockets carry the junction's conformed plane height before any road
	// tries to pin itself to one.
	Junction->RebuildJunction();

	if (bHasBehind && bHasAhead)
	{
		FRoadJunctionCut Cut;
		Cut.Junction = Junction;
		Cut.EntryDistance = static_cast<float>(Distance - ArmLength);
		Cut.ExitDistance = static_cast<float>(Distance + ArmLength);
		Cut.EntryLegIndex = BehindLeg;
		Cut.ExitLegIndex = AheadLeg;
		Road->AddJunctionCut(Cut);
	}
	else if (bHasAhead)
	{
		Road->StartJunction = Junction;
		Road->StartJunctionLegIndex = AheadLeg;
		Road->RebuildRoad();
	}
	else
	{
		Road->EndJunction = Junction;
		Road->EndJunctionLegIndex = BehindLeg;
		Road->RebuildRoad();
	}

	return Junction;
}


namespace
{
	/** Spacing at which a road centreline is sampled for crossing tests. */
	constexpr double CrossingSampleStep = 200.0;

	/** Plan-space segment crossing. */
	bool SegmentCross(
		const FVector2D& A0, const FVector2D& A1,
		const FVector2D& B0, const FVector2D& B1,
		FVector2D& OutPoint)
	{
		const FVector2D DirA = A1 - A0;
		const FVector2D DirB = B1 - B0;
		const double Denominator = DirA.X * DirB.Y - DirA.Y * DirB.X;
		if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER) return false;

		const FVector2D Delta = B0 - A0;
		const double TA = (Delta.X * DirB.Y - Delta.Y * DirB.X) / Denominator;
		const double TB = (Delta.X * DirA.Y - Delta.Y * DirA.X) / Denominator;
		if (TA < 0.0 || TA > 1.0 || TB < 0.0 || TB > 1.0) return false;

		OutPoint = A0 + DirA * TA;
		return true;
	}
}

void DetectCrossings(UWorld* World, double MergeRadius, TArray<FCrossing>& OutCrossings)
{
	OutCrossings.Reset();
	if (!World) return;

	struct FSampledRoad
	{
		ARealtimeRoadSplineActor* Road = nullptr;
		TArray<FVector2D> Points;
		double Width = 0.0;
	};

	TArray<FSampledRoad> Sampled;

	for (TActorIterator<ARealtimeRoadSplineActor> It(World); It; ++It)
	{
		ARealtimeRoadSplineActor* Road = *It;
		if (!Road || Road->bIsHolographicPreview || !Road->SplineComponent) continue;
		if (Road->SplineComponent->GetNumberOfSplinePoints() < 2) continue;

		const double Length = Road->SplineComponent->GetSplineLength();
		if (Length < CrossingSampleStep) continue;

		FSampledRoad Entry;
		Entry.Road = Road;
		Entry.Width = FMath::Max(50.0f, Road->CrossSectionSettings.RoadWidth);

		const int32 Steps = FMath::Max(2, FMath::CeilToInt(Length / CrossingSampleStep));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double D = Length * i / static_cast<double>(Steps);
			const FVector P = Road->SplineComponent->GetLocationAtDistanceAlongSpline(
				D, ESplineCoordinateSpace::World);
			Entry.Points.Add(FVector2D(P.X, P.Y));
		}

		Sampled.Add(MoveTemp(Entry));
	}

	struct FRawHit
	{
		FVector2D Location;
		ARealtimeRoadSplineActor* A = nullptr;
		ARealtimeRoadSplineActor* B = nullptr;
		double Width = 0.0;
	};
	TArray<FRawHit> Hits;

	// Every pair, every segment. Roads are few and sampled coarsely, so the quadratic cost does
	// not yet justify an acceleration structure.
	for (int32 i = 0; i < Sampled.Num(); ++i)
	{
		for (int32 j = i + 1; j < Sampled.Num(); ++j)
		{
			const FSampledRoad& RoadA = Sampled[i];
			const FSampledRoad& RoadB = Sampled[j];

			for (int32 a = 1; a < RoadA.Points.Num(); ++a)
			{
				for (int32 b = 1; b < RoadB.Points.Num(); ++b)
				{
					FVector2D Point;
					if (SegmentCross(RoadA.Points[a - 1], RoadA.Points[a],
						RoadB.Points[b - 1], RoadB.Points[b], Point))
					{
						Hits.Add({ Point, RoadA.Road, RoadB.Road,
							FMath::Max(RoadA.Width, RoadB.Width) });
					}
				}
			}
		}
	}

	// Collapse hits sitting on top of each other. Two roads crossing at a shallow angle register
	// several times along the overlap, and three roads meeting near a point give three pairwise
	// hits that are really one intersection.
	const double MergeSq = FMath::Square(FMath::Max(1.0, MergeRadius));

	for (const FRawHit& Hit : Hits)
	{
		int32 Found = INDEX_NONE;
		for (int32 c = 0; c < OutCrossings.Num(); ++c)
		{
			if (FVector2D::DistSquared(OutCrossings[c].Location, Hit.Location) < MergeSq)
			{
				Found = c;
				break;
			}
		}

		if (Found == INDEX_NONE)
		{
			FCrossing New;
			New.Location = Hit.Location;
			Found = OutCrossings.Add(MoveTemp(New));
		}
		else
		{
			// Drift toward the mean of what it absorbs, so a cluster settles in its own middle.
			OutCrossings[Found].Location = (OutCrossings[Found].Location + Hit.Location) * 0.5;
		}

		OutCrossings[Found].Roads.AddUnique(Hit.A);
		OutCrossings[Found].Roads.AddUnique(Hit.B);
		OutCrossings[Found].MaxRoadWidth = FMath::Max(OutCrossings[Found].MaxRoadWidth, Hit.Width);
	}
}

int32 RebuildIntersections(UWorld* World)
{
	if (!World) return 0;

	// A solve in flight means this came from a road rebuilding underneath one; re-entering recurses.
	if (ARealtimeRoadJunctionActor::IsAnySolveInProgress())
	{
		return 0;
	}

	TArray<FCrossing> Crossings;
	DetectCrossings(World, 900.0, Crossings);

	TArray<ARealtimeRoadJunctionActor*> Generated;
	for (TActorIterator<ARealtimeRoadJunctionActor> It(World); It; ++It)
	{
		ARealtimeRoadJunctionActor* Junction = *It;
		if (Junction && Junction->bAutoGenerated && !Junction->IsActorBeingDestroyed())
		{
			Generated.Add(Junction);
		}
	}

	TArray<ARealtimeRoadJunctionActor*> Kept;
	TWeakObjectPtr<ATerraDyneManager> GroundCache;

	for (const FCrossing& Crossing : Crossings)
	{
		if (Crossing.Roads.Num() < 2) continue;

		const FVector Target(Crossing.Location.X, Crossing.Location.Y, 0.0);

		// Reuse the nearest spare rather than churning actors on every solve.
		ARealtimeRoadJunctionActor* Junction = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (ARealtimeRoadJunctionActor* Candidate : Generated)
		{
			if (Kept.Contains(Candidate)) continue;
			const double DistSq = FVector::DistSquared2D(Candidate->GetActorLocation(), Target);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Junction = Candidate;
			}
		}

		if (!Junction)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Junction = World->SpawnActor<ARealtimeRoadJunctionActor>(
				ARealtimeRoadJunctionActor::StaticClass(), Target, FRotator::ZeroRotator, SpawnParams);
			if (!Junction) continue;
			Junction->bAutoGenerated = true;
		}

		// Bounds come from the roads meeting here rather than being authored: wide enough to gather
		// every arm and no wider, since everything inside is treated as part of this node.
		Junction->BoundsRadius = static_cast<float>(FMath::Max(400.0, Crossing.MaxRoadWidth * 1.6));
		Junction->bSolveFromNearbyRoads = true;

		// Put it on the ground. Spawning at Z zero left the node tens of metres below the roads,
		// and everything downstream that measures from it inherited that error.
		FVector Location = Target;
		float GroundZ = 0.0f;
		if (RoadMesh::SampleGroundHeight(Junction, GroundCache, Crossing.Location, GroundZ))
		{
			Location.Z = GroundZ;
		}
		Junction->SetActorLocation(Location);
		Junction->RebuildJunction();

		Kept.Add(Junction);
	}

	// Anything generated that no longer answers to a crossing gives its roads back and goes.
	for (ARealtimeRoadJunctionActor* Junction : Generated)
	{
		if (Kept.Contains(Junction)) continue;

		for (TActorIterator<ARealtimeRoadSplineActor> It(World); It; ++It)
		{
			if (ARealtimeRoadSplineActor* Road = *It)
			{
				Road->RemoveJunctionCutsFor(Junction);
			}
		}
		Junction->Destroy();
	}

	return Kept.Num();
}
}
