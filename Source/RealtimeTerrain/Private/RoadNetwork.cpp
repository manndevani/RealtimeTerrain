// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadNetwork.h"

#include "RealtimeRoadSplineActor.h"
#include "RealtimeRoadJunctionActor.h"

#include "Components/SplineComponent.h"
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

}
