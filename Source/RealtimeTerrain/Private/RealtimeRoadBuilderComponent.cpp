// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeRoadBuilderComponent.h"

#include "RealtimeRoadSplineActor.h"
#include "RealtimeRoadJunctionActor.h"
#include "RoadMeshBuilder.h"
#include "RoadNetwork.h"
#include "RoadEditOverlayActor.h"

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"
#include "Core/TerraDyneManager.h"

URealtimeRoadBuilderComponent::URealtimeRoadBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Server RPCs declared on a component only reach the server if the component replicates.
	SetIsReplicatedByDefault(true);

	// The overlay follows the cursor, so it needs a heartbeat.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void URealtimeRoadBuilderComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsDraggingPoint && SelectedRoadActor.IsValid() && SelectedPointIndex != INDEX_NONE)
	{
		FVector Ground;
		if (TraceGroundUnderCursor(Ground))
		{
			// MovePointRealtime refreshes the render mesh now and defers the collision cook, which
			// is what keeps a drag smooth; EndPointDrag forces the real rebuild.
			SelectedRoadActor->MovePointRealtime(SelectedPointIndex, Ground);
		}
	}

	UpdateEditOverlay();
}

void URealtimeRoadBuilderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EditOverlay)
	{
		EditOverlay->Destroy();
		EditOverlay = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

APlayerController* URealtimeRoadBuilderComponent::GetOwningPlayerController() const
{
	if (APlayerController* AsController = Cast<APlayerController>(GetOwner()))
	{
		return AsController;
	}
	if (const APawn* AsPawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}
	return nullptr;
}

bool URealtimeRoadBuilderComponent::TraceHandleUnderCursor(
	ARealtimeRoadSplineActor*& OutRoad, int32& OutPointIndex) const
{
	OutRoad = nullptr;
	OutPointIndex = INDEX_NONE;

	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || !EditOverlay) return false;

	FHitResult Hit;
	if (!Controller->GetHitResultUnderCursorByChannel(
		UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit))
	{
		return false;
	}

	const int32 Index = EditOverlay->GetPointIndexFromComponent(Hit.GetComponent());
	if (Index == INDEX_NONE) return false;

	OutRoad = HoveredRoadActor.IsValid() ? HoveredRoadActor.Get() : SelectedRoadActor.Get();
	OutPointIndex = Index;
	return OutRoad != nullptr;
}

bool URealtimeRoadBuilderComponent::TraceGroundUnderCursor(FVector& OutLocation) const
{
	APlayerController* Controller = GetOwningPlayerController();
	UWorld* World = GetWorld();
	if (!Controller || !World) return false;

	FVector Origin;
	FVector Direction;
	if (!Controller->DeprojectMousePositionToWorld(Origin, Direction)) return false;

	// The overlay blocks the visibility channel so its handles are grabbable; it must not also
	// swallow the ground trace that decides where a dragged point goes.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RoadBuilderCursorGround), false);
	Params.AddIgnoredActor(EditOverlay);
	if (SelectedRoadActor.IsValid())
	{
		Params.AddIgnoredActor(SelectedRoadActor.Get());
	}
	if (ActiveDraftRoad)
	{
		Params.AddIgnoredActor(ActiveDraftRoad);
	}

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(
		Hit, Origin, Origin + Direction * 5000000.0, ECC_WorldStatic, Params))
	{
		return false;
	}

	OutLocation = ProjectOntoTerrain(Hit.Location);
	return true;
}

void URealtimeRoadBuilderComponent::UpdateEditOverlay()
{
	if (!bShowEditOverlay)
	{
		if (EditOverlay)
		{
			EditOverlay->Destroy();
			EditOverlay = nullptr;
		}
		HoveredRoadActor = nullptr;
		HoveredPointIndex = INDEX_NONE;
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	if (!EditOverlay)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		EditOverlay = World->SpawnActor<ARoadEditOverlayActor>(
			ARoadEditOverlayActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!EditOverlay) return;
	}

	// Whichever road the tool is working on: the selection, else the ghost being drawn, else
	// whatever the cursor is over. Showing handles for the hovered road is what makes it possible
	// to grab a point on a road that is not selected yet.
	ARealtimeRoadSplineActor* Target = SelectedRoadActor.Get();
	if (!Target)
	{
		Target = ActiveDraftRoad;
	}
	if (!Target && !bIsDraggingPoint)
	{
		FVector Ground;
		if (TraceGroundUnderCursor(Ground))
		{
			ARealtimeRoadSplineActor* NearRoad = nullptr;
			float Along = 0.0f;
			if (FindRoadLineNear(Ground, NearRoad, Along))
			{
				Target = NearRoad;
			}
		}
	}

	// A long road is hundreds of instances; refilling them at frame rate for a static spline is
	// pure waste. Rebuild on a change of target, or while a drag is actually moving the points.
	if (Target != OverlayBuiltForRoad.Get() || bIsDraggingPoint)
	{
		EditOverlay->ShowRoad(Target);
		OverlayBuiltForRoad = Target;
	}

	EditOverlay->SetSelectedPoint(
		(Target && Target == SelectedRoadActor.Get()) ? SelectedPointIndex : INDEX_NONE);

	HoveredRoadActor = Target;

	ARealtimeRoadSplineActor* HandleRoad = nullptr;
	int32 HandleIndex = INDEX_NONE;
	if (!bIsDraggingPoint && TraceHandleUnderCursor(HandleRoad, HandleIndex))
	{
		HoveredPointIndex = HandleIndex;
	}
	else
	{
		HoveredPointIndex = INDEX_NONE;
	}

	EditOverlay->SetHighlightedPoint(HoveredPointIndex);
}

bool URealtimeRoadBuilderComponent::BeginPointDrag()
{
	// Only a press that actually lands on a handle begins a drag. Starting one just because
	// something happens to be selected would consume every subsequent click, and clicking ahead
	// of a road to extend it would silently do nothing.
	if (!HoveredRoadActor.IsValid() || HoveredPointIndex == INDEX_NONE)
	{
		return false;
	}

	SelectRoadPoint(HoveredRoadActor.Get(), HoveredPointIndex);
	bIsDraggingPoint = true;
	return true;
}

void URealtimeRoadBuilderComponent::EndPointDrag()
{
	if (!bIsDraggingPoint) return;

	bIsDraggingPoint = false;
	OverlayBuiltForRoad = nullptr;

	// Settle now rather than waiting out the road's timer, so collision matches the final position
	// the moment the player lets go.
	if (SelectedRoadActor.IsValid())
	{
		SelectedRoadActor->RebuildRoad();
	}
}

bool URealtimeRoadBuilderComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->HasAuthority() : true;
}

void URealtimeRoadBuilderComponent::SelectRoadPoint(ARealtimeRoadSplineActor* RoadActor, int32 PointIndex)
{
	// Selecting can insert a point, so the overlay geometry is stale either way.
	OverlayBuiltForRoad = nullptr;

	SelectedRoadActor = RoadActor;
	SelectedPointIndex = PointIndex;
	OnRoadPointSelected.Broadcast(RoadActor, PointIndex);
}

void URealtimeRoadBuilderComponent::ClearSelection()
{
	SelectedRoadActor = nullptr;
	SelectedPointIndex = INDEX_NONE;
	OnRoadPointSelected.Broadcast(nullptr, INDEX_NONE);
}

FVector URealtimeRoadBuilderComponent::ProjectOntoTerrain(const FVector& WorldLocation) const
{
	float GroundZ = 0.0f;
	if (RoadMesh::SampleGroundHeight(GetOwner(), CachedTerraDyneManager,
		FVector2D(WorldLocation.X, WorldLocation.Y), GroundZ))
	{
		return FVector(WorldLocation.X, WorldLocation.Y, GroundZ);
	}
	return WorldLocation;
}

bool URealtimeRoadBuilderComponent::FindRoadPointNear(
	const FVector& WorldLocation,
	ARealtimeRoadSplineActor*& OutRoad,
	int32& OutPointIndex) const
{
	OutRoad = nullptr;
	OutPointIndex = INDEX_NONE;

	UWorld* World = GetWorld();
	if (!World) return false;

	float BestDistance = PointSelectionRadius;
	for (TActorIterator<ARealtimeRoadSplineActor> It(World); It; ++It)
	{
		ARealtimeRoadSplineActor* Road = *It;
		if (!Road || Road->bIsHolographicPreview || Road == ActiveDraftRoad)
		{
			continue;
		}

		float Distance = MAX_FLT;
		const int32 PointIndex = Road->FindClosestSplinePoint(WorldLocation, Distance);
		if (PointIndex != INDEX_NONE && Distance < BestDistance)
		{
			BestDistance = Distance;
			OutRoad = Road;
			OutPointIndex = PointIndex;
		}
	}

	return OutRoad != nullptr;
}

bool URealtimeRoadBuilderComponent::FindRoadLineNear(
	const FVector& WorldLocation,
	ARealtimeRoadSplineActor*& OutRoad,
	float& OutDistanceAlong) const
{
	OutRoad = nullptr;
	OutDistanceAlong = 0.0f;

	UWorld* World = GetWorld();
	if (!World) return false;

	double BestDistance = RoadLineSnapRadius;

	for (TActorIterator<ARealtimeRoadSplineActor> It(World); It; ++It)
	{
		ARealtimeRoadSplineActor* Road = *It;
		if (!Road || Road->bIsHolographicPreview || Road == ActiveDraftRoad || !Road->SplineComponent)
		{
			continue;
		}

		double Along = 0.0;
		if (!RoadNetwork::FindDistanceAlongRoad(Road, WorldLocation, Along))
		{
			continue;
		}

		// Compared in plan only: the click comes off a terrain trace and the road sits above the
		// ground, so a height difference should not stop it counting as a hit.
		const FVector OnRoad = Road->SplineComponent->GetLocationAtDistanceAlongSpline(
			Along, ESplineCoordinateSpace::World);
		const double Planar = FVector::Dist2D(OnRoad, WorldLocation);

		if (Planar < BestDistance)
		{
			BestDistance = Planar;
			OutRoad = Road;
			OutDistanceAlong = static_cast<float>(Along);
		}
	}

	return OutRoad != nullptr;
}

ARealtimeRoadJunctionActor* URealtimeRoadBuilderComponent::CreateJunctionOnRoadAt(
	ARealtimeRoadSplineActor* Road,
	const FVector& WorldLocation)
{
	if (!Road) return nullptr;

	double Along = 0.0;
	if (!RoadNetwork::FindDistanceAlongRoad(Road, WorldLocation, Along))
	{
		return nullptr;
	}

	TArray<int32> UnusedLegs;
	return RoadNetwork::CreateJunctionOnRoad(Road, Along, TArray<float>(), UnusedLegs);
}

FVector URealtimeRoadBuilderComponent::ResolvePlacementLocation(
	const FVector& RawLocation,
	ARealtimeRoadJunctionActor*& OutSnappedJunction,
	int32& OutSnappedLeg) const
{
	OutSnappedJunction = nullptr;
	OutSnappedLeg = INDEX_NONE;

	UWorld* World = GetWorld();
	if (!World) return RawLocation;

	float BestDistance = JunctionSnapRadius;
	for (TActorIterator<ARealtimeRoadJunctionActor> It(World); It; ++It)
	{
		ARealtimeRoadJunctionActor* Junction = *It;
		if (!Junction || Junction->bIsHolographicPreview)
		{
			continue;
		}

		int32 LegIndex = INDEX_NONE;
		float LegDistance = MAX_FLT;
		if (Junction->FindClosestLeg(RawLocation, LegIndex, LegDistance) && LegDistance < BestDistance)
		{
			BestDistance = LegDistance;
			OutSnappedJunction = Junction;
			OutSnappedLeg = LegIndex;
		}
	}

	if (OutSnappedJunction && OutSnappedLeg != INDEX_NONE)
	{
		FTransform Socket;
		if (OutSnappedJunction->GetLegConnectionTransform(OutSnappedLeg, Socket))
		{
			return Socket.GetLocation();
		}
	}

	// No junction in range, so the point belongs on the ground. The old implementation was named
	// for this and returned the raw click location unchanged, which is why freshly drawn roads
	// started at whatever height the click ray reported.
	return bSnapToTerrain ? ProjectOntoTerrain(RawLocation) : RawLocation;
}

void URealtimeRoadBuilderComponent::HandleWorldClick(const FVector& ClickedWorldLocation)
{
	if (!GetWorld()) return;

	// An actual handle under the cursor is exact, so it beats the radius search below.
	if (HoveredRoadActor.IsValid() && HoveredPointIndex != INDEX_NONE)
	{
		SelectRoadPoint(HoveredRoadActor.Get(), HoveredPointIndex);
		return;
	}

	// Clicking an existing point always re-anchors the selection. Gating this on "nothing selected
	// yet" meant that once you had picked a point you could never pick a different one.
	ARealtimeRoadSplineActor* HitRoad = nullptr;
	int32 HitPointIndex = INDEX_NONE;
	if (FindRoadPointNear(ClickedWorldLocation, HitRoad, HitPointIndex))
	{
		SelectRoadPoint(HitRoad, HitPointIndex);
		return;
	}

	// A click on a road's line but away from its points inserts an anchor there. Without this you
	// could only ever branch from wherever control points happened to fall, which on a road whose
	// points are 8 metres apart is not the point you clicked.
	if (!SelectedRoadActor.IsValid())
	{
		ARealtimeRoadSplineActor* LineRoad = nullptr;
		float LineDistance = 0.0f;
		if (FindRoadLineNear(ClickedWorldLocation, LineRoad, LineDistance))
		{
			const int32 Inserted = LineRoad->InsertPointAtDistance(LineDistance);
			if (Inserted != INDEX_NONE)
			{
				SelectRoadPoint(LineRoad, Inserted);
				return;
			}
		}
	}

	if (SelectedRoadActor.IsValid() && SelectedPointIndex != INDEX_NONE)
	{
		ARealtimeRoadSplineActor* Road = SelectedRoadActor.Get();

		ARealtimeRoadJunctionActor* SnappedJunction = nullptr;
		int32 SnappedLeg = INDEX_NONE;
		const FVector Destination = ResolvePlacementLocation(ClickedWorldLocation, SnappedJunction, SnappedLeg);

		const bool bExtendingFromStart = (SelectedPointIndex == 0);
		Road->ExtendFromPointToLocation(SelectedPointIndex, Destination, AutoPointSpacing);

		if (SnappedJunction)
		{
			// Record the attachment so the road's end stays pinned to the leg socket on every
			// later rebuild, not just at the moment of the click.
			if (bExtendingFromStart)
			{
				Road->StartJunction = SnappedJunction;
				Road->StartJunctionLegIndex = SnappedLeg;
			}
			else
			{
				Road->EndJunction = SnappedJunction;
				Road->EndJunctionLegIndex = SnappedLeg;
			}
			Road->RebuildRoad();
		}

		// Follow the end that just grew, so consecutive clicks keep extending in the same direction.
		if (Road->SplineComponent)
		{
			SelectedPointIndex = bExtendingFromStart
				? 0
				: Road->SplineComponent->GetNumberOfSplinePoints() - 1;
		}

		OnRoadPlaced.Broadcast(Road);
		return;
	}

	// Nothing selected and nothing under the cursor: start a road here.
	//
	// This spawns a committed road rather than a holographic draft. A draft is excluded from both
	// FindRoadPointNear and FindRoadLineNear above, so the next click could never find the thing
	// just started — every click simply replaced the ghost and no road was ever finished.
	ARealtimeRoadJunctionActor* StartJunctionHit = nullptr;
	int32 StartLeg = INDEX_NONE;
	const FVector Start = ResolvePlacementLocation(ClickedWorldLocation, StartJunctionHit, StartLeg);

	if (ARealtimeRoadSplineActor* NewRoad = StartRoadAt(Start, DefaultRoadType))
	{
		if (StartJunctionHit)
		{
			NewRoad->StartJunction = StartJunctionHit;
			NewRoad->StartJunctionLegIndex = StartLeg;
		}
		OnRoadPlaced.Broadcast(NewRoad);
	}
}

ARealtimeRoadSplineActor* URealtimeRoadBuilderComponent::StartRoadAt(const FVector& WorldLocation, ERoadType RoadType)
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>(
		ARealtimeRoadSplineActor::StaticClass(), WorldLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Road) return nullptr;

	Road->RoadType = RoadType;

	// One point builds no mesh — a road needs two stations to sweep. That is expected: the second
	// click is what makes it appear.
	TArray<FVector> Seed;
	Seed.Add(WorldLocation);
	Road->SetPointsRealtime(Seed);

	SelectRoadPoint(Road, 0);
	return Road;
}

bool URealtimeRoadBuilderComponent::SelectRoadNear(const FVector& WorldLocation)
{
	// An exact handle hit is unambiguous, so it wins.
	if (HoveredRoadActor.IsValid() && HoveredPointIndex != INDEX_NONE)
	{
		SelectRoadPoint(HoveredRoadActor.Get(), HoveredPointIndex);
		return true;
	}

	ARealtimeRoadSplineActor* Road = nullptr;
	int32 PointIndex = INDEX_NONE;
	if (FindRoadPointNear(WorldLocation, Road, PointIndex))
	{
		SelectRoadPoint(Road, PointIndex);
		return true;
	}

	float Along = 0.0f;
	if (FindRoadLineNear(WorldLocation, Road, Along))
	{
		const int32 Inserted = Road->InsertPointAtDistance(Along);
		if (Inserted != INDEX_NONE)
		{
			SelectRoadPoint(Road, Inserted);
			return true;
		}
	}

	ClearSelection();
	return false;
}

ARealtimeRoadSplineActor* URealtimeRoadBuilderComponent::BeginRoadDraft(const FVector& StartWorldLocation, ERoadType RoadType)
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	if (ActiveDraftRoad)
	{
		CancelRoadDraft();
	}

	ARealtimeRoadJunctionActor* SnappedJunction = nullptr;
	int32 SnappedLeg = INDEX_NONE;
	const FVector Start = ResolvePlacementLocation(StartWorldLocation, SnappedJunction, SnappedLeg);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	ActiveDraftRoad = World->SpawnActor<ARealtimeRoadSplineActor>(
		ARealtimeRoadSplineActor::StaticClass(), Start, FRotator::ZeroRotator, SpawnParams);
	if (!ActiveDraftRoad) return nullptr;

	ActiveDraftRoad->RoadType = RoadType;
	ActiveDraftRoad->SetIsHolographicPreview(true);

	if (SnappedJunction)
	{
		ActiveDraftRoad->StartJunction = SnappedJunction;
		ActiveDraftRoad->StartJunctionLegIndex = SnappedLeg;
	}

	// A spline needs two points before it has a direction to sweep along.
	const FVector Second = ProjectOntoTerrain(Start + FVector(200.0, 0.0, 0.0));
	ActiveDraftRoad->SetPointsRealtime({ Start, Second });

	return ActiveDraftRoad;
}

void URealtimeRoadBuilderComponent::UpdateRoadDraft(const FVector& CursorWorldLocation)
{
	if (!ActiveDraftRoad || !ActiveDraftRoad->SplineComponent) return;

	const int32 NumPoints = ActiveDraftRoad->SplineComponent->GetNumberOfSplinePoints();
	if (NumPoints < 2) return;

	ARealtimeRoadJunctionActor* SnappedJunction = nullptr;
	int32 SnappedLeg = INDEX_NONE;
	const FVector End = ResolvePlacementLocation(CursorWorldLocation, SnappedJunction, SnappedLeg);

	ActiveDraftRoad->EndJunction = SnappedJunction;
	ActiveDraftRoad->EndJunctionLegIndex = SnappedLeg;

	// MovePointRealtime refreshes the render mesh immediately and defers the collision cook until
	// the drag settles, which is what makes this safe to drive from a per-frame cursor update.
	ActiveDraftRoad->MovePointRealtime(NumPoints - 1, End);
}

void URealtimeRoadBuilderComponent::CommitPoint(const FVector& PointWorldLocation)
{
	if (!ActiveDraftRoad) return;

	ARealtimeRoadJunctionActor* SnappedJunction = nullptr;
	int32 SnappedLeg = INDEX_NONE;
	const FVector Point = ResolvePlacementLocation(PointWorldLocation, SnappedJunction, SnappedLeg);

	ActiveDraftRoad->AddPointRealtime(Point);
}

ARealtimeRoadSplineActor* URealtimeRoadBuilderComponent::FinishRoadDraft()
{
	if (!ActiveDraftRoad || !ActiveDraftRoad->SplineComponent) return nullptr;

	const int32 NumPoints = ActiveDraftRoad->SplineComponent->GetNumberOfSplinePoints();
	TArray<FVector> Points;
	Points.Reserve(NumPoints);
	for (int32 i = 0; i < NumPoints; ++i)
	{
		Points.Add(ActiveDraftRoad->GetSplinePointWorldLocation(i));
	}

	const ERoadType DraftType = ActiveDraftRoad->RoadType;

	// The ghost is transient and local either way; the committed road is a separate replicated
	// actor rather than the preview promoted in place.
	ActiveDraftRoad->Destroy();
	ActiveDraftRoad = nullptr;

	if (Points.Num() < 2)
	{
		ClearSelection();
		return nullptr;
	}

	if (!HasAuthority())
	{
		Server_CommitRoad(Points, DraftType);
		ClearSelection();
		return nullptr;
	}

	ARealtimeRoadSplineActor* Road = SpawnRoadAuthoritative(Points, DraftType);
	if (Road)
	{
		SelectRoadPoint(Road, Road->SplineComponent
			? Road->SplineComponent->GetNumberOfSplinePoints() - 1
			: 0);
		OnRoadPlaced.Broadcast(Road);
	}
	return Road;
}

void URealtimeRoadBuilderComponent::CancelRoadDraft()
{
	if (ActiveDraftRoad)
	{
		ActiveDraftRoad->Destroy();
		ActiveDraftRoad = nullptr;
	}
	ClearSelection();
}

ARealtimeRoadSplineActor* URealtimeRoadBuilderComponent::SpawnRoadAuthoritative(const TArray<FVector>& WorldPoints, ERoadType RoadType)
{
	UWorld* World = GetWorld();
	if (!World || WorldPoints.Num() < 2) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARealtimeRoadSplineActor* Road = World->SpawnActor<ARealtimeRoadSplineActor>(
		ARealtimeRoadSplineActor::StaticClass(), WorldPoints[0], FRotator::ZeroRotator, SpawnParams);
	if (!Road) return nullptr;

	Road->RoadType = RoadType;
	Road->SetPointsRealtime(WorldPoints);
	Road->RebuildRoad();
	return Road;
}

void URealtimeRoadBuilderComponent::Server_CommitRoad_Implementation(const TArray<FVector>& WorldPoints, ERoadType RoadType)
{
	if (ARealtimeRoadSplineActor* Road = SpawnRoadAuthoritative(WorldPoints, RoadType))
	{
		OnRoadPlaced.Broadcast(Road);
	}
}

ARealtimeRoadJunctionActor* URealtimeRoadBuilderComponent::SpawnJunctionAtLocation(const FVector& WorldLocation, ERoadJunctionType JunctionType)
{
	if (!HasAuthority())
	{
		Server_SpawnJunction(WorldLocation, JunctionType);
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World) return nullptr;

	const FVector Grounded = bSnapToTerrain ? ProjectOntoTerrain(WorldLocation) : WorldLocation;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARealtimeRoadJunctionActor* Junction = World->SpawnActor<ARealtimeRoadJunctionActor>(
		ARealtimeRoadJunctionActor::StaticClass(), Grounded, FRotator::ZeroRotator, SpawnParams);
	if (Junction)
	{
		Junction->JunctionType = JunctionType;
		// Presets have to be expanded explicitly; the constructor ran before JunctionType was set.
		Junction->SyncPresetLegs();
		Junction->RebuildJunction();
	}
	return Junction;
}

void URealtimeRoadBuilderComponent::Server_SpawnJunction_Implementation(FVector WorldLocation, ERoadJunctionType JunctionType)
{
	SpawnJunctionAtLocation(WorldLocation, JunctionType);
}
