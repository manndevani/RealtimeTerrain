// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RealtimeRoadBuilderComponent.h"

#include "RealtimeRoadSplineActor.h"
#include "RealtimeRoadJunctionActor.h"
#include "RoadMeshBuilder.h"
#include "RoadNetwork.h"
#include "RoadEditOverlayActor.h"

#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"
#include "Core/TerraDyneManager.h"
#include "Building/TerraDyneGizmoActor.h"
#include "Camera/PlayerCameraManager.h"

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
		if (DragAxis != ETerraDyneGizmoAxis::None)
		{
			UpdateAxisDrag();
		}
		else
		{
			FVector Ground;
			if (TraceGroundUnderCursor(Ground))
			{
				// Carry the grab offset in plan so the point tracks cursor movement rather than
				// teleporting under it, and take height from the ground it lands on.
				const FVector Target = ProjectOntoTerrain(FVector(
					Ground.X + DragGrabOffset.X,
					Ground.Y + DragGrabOffset.Y,
					Ground.Z));

				// MovePointRealtime refreshes the render mesh now and defers the collision cook,
				// which is what keeps a drag smooth; EndPointDrag forces the real rebuild.
				SelectedRoadActor->MovePointRealtime(SelectedPointIndex, Target);
			}
		}

		// Junctions follow the road as it moves, so the intersection reshapes under the cursor
		// rather than snapping once the drag ends. Throttled: a re-solve rebuilds the junction
		// surface and rewrites road cuts, which is far too heavy to run every frame.
		TimeSinceJunctionResolve += DeltaTime;
		if (TimeSinceJunctionResolve >= FMath::Max(0.0f, JunctionResolveInterval))
		{
			TimeSinceJunctionResolve = 0.0f;
			SelectedRoadActor->RefreshAffectedJunctions();
		}
	}

	UpdateEditOverlay();
	UpdatePointGizmo();
}

void URealtimeRoadBuilderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EditOverlay)
	{
		EditOverlay->Destroy();
		EditOverlay = nullptr;
	}
	if (PointGizmo)
	{
		PointGizmo->Destroy();
		PointGizmo = nullptr;
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
	UWorld* World = GetWorld();
	if (!Controller || !World || !EditOverlay) return false;

	FVector Origin;
	FVector Direction;
	if (!Controller->DeprojectMousePositionToWorld(Origin, Direction)) return false;

	// A single-hit trace resolves to whatever's geometrically closest along the ray -- usually
	// terrain, since the overlay deliberately renders its handles through terrain and other roads
	// (SDPG_Foreground, disabled depth test) so they stay visible and grabbable even when something
	// else sits between them and the camera. A multi-hit trace lets a handle win the hit-test
	// whenever one is anywhere along the ray, which is what the player actually sees on screen.
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Origin, Origin + Direction * 5000000.0, ECC_Visibility);

	for (const FHitResult& Hit : Hits)
	{
		const int32 Index = EditOverlay->GetPointIndexFromComponent(Hit.GetComponent());
		if (Index != INDEX_NONE)
		{
			OutRoad = HoveredRoadActor.IsValid() ? HoveredRoadActor.Get() : SelectedRoadActor.Get();
			OutPointIndex = Index;
			return OutRoad != nullptr;
		}
	}
	return false;
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
	Params.AddIgnoredActor(PointGizmo);
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

void URealtimeRoadBuilderComponent::UpdatePointGizmo()
{
	const bool bWanted =
		bShowPointGizmo && SelectedRoadActor.IsValid() && SelectedPointIndex != INDEX_NONE;

	if (!bWanted)
	{
		if (PointGizmo)
		{
			PointGizmo->SetActorHiddenInGame(true);
			PointGizmo->SetActorEnableCollision(false);
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	if (!PointGizmo)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		PointGizmo = World->SpawnActor<ATerraDyneGizmoActor>(
			ATerraDyneGizmoActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!PointGizmo) return;
	}

	PointGizmo->SetActorHiddenInGame(false);
	PointGizmo->SetActorEnableCollision(true);

	// The visible handle sphere is lifted by EditOverlay->VerticalOffset (ShowRoad) so it reads
	// clearly against the road surface; the gizmo has to match that lift or it sits visibly below
	// the handle it's supposed to be centered on.
	FVector GizmoLocation = SelectedRoadActor->GetSplinePointWorldLocation(SelectedPointIndex);
	if (EditOverlay)
	{
		GizmoLocation.Z += EditOverlay->VerticalOffset;
	}
	PointGizmo->SetActorLocation(GizmoLocation);

	// Mid-drag the highlight stays on the axis being dragged, even once the cursor has left it.
	if (bIsDraggingPoint)
	{
		PointGizmo->SetHighlightedAxis(DragAxis);
	}
	else
	{
		ETerraDyneGizmoAxis Hovered = ETerraDyneGizmoAxis::None;
		TraceGizmoAxisUnderCursor(Hovered);
		PointGizmo->SetHighlightedAxis(Hovered);
	}
}

bool URealtimeRoadBuilderComponent::TraceGizmoAxisUnderCursor(ETerraDyneGizmoAxis& OutAxis) const
{
	OutAxis = ETerraDyneGizmoAxis::None;

	APlayerController* Controller = GetOwningPlayerController();
	UWorld* World = GetWorld();
	if (!Controller || !World || !PointGizmo || PointGizmo->IsHidden())
	{
		return false;
	}

	FVector Origin;
	FVector Direction;
	if (!Controller->DeprojectMousePositionToWorld(Origin, Direction)) return false;

	// Same reasoning as TraceHandleUnderCursor: a single-hit trace resolves to whatever's closest
	// along the ray, and the point-handle sphere (45cm radius, a different actor) sits at the same
	// world position as the gizmo's CenterXY disc (20cm radius) and geometrically contains it from
	// most viewing angles -- so a single-hit trace would resolve to the sphere and CenterXY would
	// never be reachable at all. Walking every hit in order and skipping ones that aren't part of
	// this gizmo lets the disc (or an axis arm behind something else) still win.
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Origin, Origin + Direction * 5000000.0, ECC_Visibility);

	for (const FHitResult& Hit : Hits)
	{
		const ETerraDyneGizmoAxis Axis = PointGizmo->GetAxisFromComponent(Hit.GetComponent());
		if (Axis != ETerraDyneGizmoAxis::None)
		{
			OutAxis = Axis;
			return true;
		}
	}
	return false;
}

bool URealtimeRoadBuilderComponent::IntersectCursorWithPlane(const FPlane& Plane, FVector& OutIntersection) const
{
	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller) return false;

	FVector Origin;
	FVector Direction;
	if (!Controller->DeprojectMousePositionToWorld(Origin, Direction)) return false;

	OutIntersection = FMath::RayPlaneIntersection(Origin, Direction, Plane);
	return true;
}

void URealtimeRoadBuilderComponent::UpdateAxisDrag()
{
	FVector Intersection;
	if (!IntersectCursorWithPlane(DragPlane, Intersection)) return;

	const FVector Delta = Intersection - DragStartIntersection;
	FVector NewLocation = DragStartPointLocation;

	switch (DragAxis)
	{
	case ETerraDyneGizmoAxis::X:
		NewLocation.X += Delta.X;
		break;
	case ETerraDyneGizmoAxis::Y:
		NewLocation.Y += Delta.Y;
		break;
	case ETerraDyneGizmoAxis::Z:
		NewLocation.Z += Delta.Z;
		break;
	case ETerraDyneGizmoAxis::CenterXY:
		NewLocation.X += Delta.X;
		NewLocation.Y += Delta.Y;
		break;
	default:
		return;
	}

	// Axis drags deliberately skip the terrain snap a handle drag uses. Constraining motion to one
	// axis and then dropping the result onto the ground would make the Z arrow do nothing at all.
	SelectedRoadActor->MovePointRealtime(SelectedPointIndex, NewLocation);
}

bool URealtimeRoadBuilderComponent::BeginPointDrag()
{
	// An axis arrow wins over a point handle: it is the smaller, more deliberate target, it only
	// exists on the already-selected point, and it is the only way to move a point somewhere the
	// ground trace would never put it.
	ETerraDyneGizmoAxis Axis = ETerraDyneGizmoAxis::None;
	if (SelectedRoadActor.IsValid() && SelectedPointIndex != INDEX_NONE && TraceGizmoAxisUnderCursor(Axis))
	{
		DragAxis = Axis;
		DragStartPointLocation = SelectedRoadActor->GetSplinePointWorldLocation(SelectedPointIndex);

		// Z drags against a vertical plane turned to face the camera, so cursor motion maps to
		// height. The others drag against the horizontal plane through the point.
		FVector Normal = FVector::UpVector;
		if (Axis == ETerraDyneGizmoAxis::Z)
		{
			const APlayerController* Controller = GetOwningPlayerController();
			const FVector CameraLocation = (Controller && Controller->PlayerCameraManager)
				? Controller->PlayerCameraManager->GetCameraLocation()
				: FVector::ZeroVector;

			const FVector Facing = (CameraLocation - DragStartPointLocation).GetSafeNormal2D();
			Normal = Facing.IsNearlyZero() ? FVector::ForwardVector : Facing;
		}

		DragPlane = FPlane(DragStartPointLocation, Normal);
		IntersectCursorWithPlane(DragPlane, DragStartIntersection);

		RecordUndo(SelectedRoadActor.Get());

		bIsDraggingPoint = true;
		return true;
	}

	// Otherwise a press has to land on a handle. A fresh trace here, rather than trusting
	// HoveredRoadActor/HoveredPointIndex (last set by UpdateEditOverlay on the previous tick), so a
	// press right after a fast cursor move can't act on a hover state that's already stale by a
	// frame -- the axis check above and this one now both look at the world as of this exact press.
	ARealtimeRoadSplineActor* PressedRoad = nullptr;
	int32 PressedPointIndex = INDEX_NONE;
	if (!TraceHandleUnderCursor(PressedRoad, PressedPointIndex))
	{
		return false;
	}

	// A first press on any handle both selects it and starts moving it in one motion -- the
	// interaction every 3D editor gizmo uses. Requiring a second, separate press to actually drag
	// used to be the "safety" here, on the theory that picking a point could otherwise fling it; the
	// grab-offset math below already makes a direct press-drag safe on its own (the point tracks the
	// cursor from wherever it already is, not wherever the cursor first clicked, so a stationary
	// click moves nothing), so the extra press was just friction.
	if (SelectedRoadActor.Get() != PressedRoad || SelectedPointIndex != PressedPointIndex)
	{
		SelectRoadPoint(PressedRoad, PressedPointIndex);
	}

	DragAxis = ETerraDyneGizmoAxis::None;
	DragStartPointLocation = SelectedRoadActor->GetSplinePointWorldLocation(SelectedPointIndex);

	// Once, here, rather than per frame: undo should step back to where the point started, not
	// crawl back along the path it was dragged over.
	RecordUndo(SelectedRoadActor.Get());

	// Grab where the point already is, relative to the cursor, so it moves with the cursor instead
	// of jumping to it. If the very first trace of the drag fails (cursor briefly over a gap/skybox
	// on press), leave DragGrabOffset as whatever it already held rather than forcing it to zero --
	// zero means "grab exactly at the ground hit", which would make the point visibly snap to sit
	// under the cursor the moment a trace next succeeds, instead of tracking from where it was.
	FVector Ground;
	if (TraceGroundUnderCursor(Ground))
	{
		DragGrabOffset = DragStartPointLocation - Ground;
	}

	bIsDraggingPoint = true;
	return true;
}

void URealtimeRoadBuilderComponent::EndPointDrag()
{
	if (!bIsDraggingPoint) return;

	bIsDraggingPoint = false;
	DragAxis = ETerraDyneGizmoAxis::None;
	DragGrabOffset = FVector::ZeroVector;
	OverlayBuiltForRoad = nullptr;
	TimeSinceJunctionResolve = 0.0f;

	if (PointGizmo)
	{
		PointGizmo->SetHighlightedAxis(ETerraDyneGizmoAxis::None);
	}

	// Settle now rather than waiting out the road's timer, so collision matches the final position
	// the moment the player lets go.
	if (SelectedRoadActor.IsValid())
	{
		SelectedRoadActor->RebuildRoad();
		SelectedRoadActor->RefreshAffectedJunctions();
	}
}

bool URealtimeRoadBuilderComponent::HasPointSelected() const
{
	return SelectedRoadActor.IsValid() && SelectedPointIndex != INDEX_NONE;
}

bool URealtimeRoadBuilderComponent::CaptureSnapshot(
	ARealtimeRoadSplineActor* Road, FRoadEditSnapshot& OutSnapshot) const
{
	if (!Road || !Road->SplineComponent) return false;

	const int32 Num = Road->SplineComponent->GetNumberOfSplinePoints();
	if (Num == 0) return false;

	OutSnapshot.Road = Road;
	OutSnapshot.Points.Reset(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		OutSnapshot.Points.Add(Road->GetSplinePointWorldLocation(i));
	}
	return true;
}

bool URealtimeRoadBuilderComponent::ApplySnapshot(const FRoadEditSnapshot& Snapshot)
{
	ARealtimeRoadSplineActor* Road = Snapshot.Road.Get();
	if (!Road || Snapshot.Points.Num() < 1) return false;

	Road->SetPointsRealtime(Snapshot.Points);
	Road->RebuildRoad();

	// Intersections were solved against the old shape, so they have to be reconciled against this
	// one or the road would snap back while its junctions stayed where they were.
	Road->RefreshAffectedJunctions();

	SelectedRoadActor = Road;
	SelectedPointIndex = FMath::Clamp(SelectedPointIndex, 0, Snapshot.Points.Num() - 1);
	OverlayBuiltForRoad = nullptr;
	return true;
}

void URealtimeRoadBuilderComponent::RecordUndo(ARealtimeRoadSplineActor* Road)
{
	FRoadEditSnapshot Snapshot;
	if (!CaptureSnapshot(Road, Snapshot)) return;

	UndoStack.Add(MoveTemp(Snapshot));

	// A fresh edit invalidates anything that was undone past.
	RedoStack.Reset();

	const int32 Limit = FMath::Max(1, MaxUndoSteps);
	while (UndoStack.Num() > Limit)
	{
		UndoStack.RemoveAt(0);
	}
}

bool URealtimeRoadBuilderComponent::UndoRoadEdit()
{
	while (UndoStack.Num() > 0)
	{
		FRoadEditSnapshot Snapshot = UndoStack.Pop();
		if (!Snapshot.Road.IsValid())
		{
			// The road was destroyed since; that step can never be replayed.
			continue;
		}

		FRoadEditSnapshot Current;
		if (CaptureSnapshot(Snapshot.Road.Get(), Current))
		{
			RedoStack.Add(MoveTemp(Current));
		}

		return ApplySnapshot(Snapshot);
	}
	return false;
}

bool URealtimeRoadBuilderComponent::RedoRoadEdit()
{
	while (RedoStack.Num() > 0)
	{
		FRoadEditSnapshot Snapshot = RedoStack.Pop();
		if (!Snapshot.Road.IsValid())
		{
			continue;
		}

		FRoadEditSnapshot Current;
		if (CaptureSnapshot(Snapshot.Road.Get(), Current))
		{
			UndoStack.Add(MoveTemp(Current));
		}

		return ApplySnapshot(Snapshot);
	}
	return false;
}

bool URealtimeRoadBuilderComponent::DeleteSelectedPoint()
{
	if (!HasPointSelected()) return false;

	ARealtimeRoadSplineActor* Road = SelectedRoadActor.Get();
	if (!Road || !Road->SplineComponent) return false;

	const int32 Num = Road->SplineComponent->GetNumberOfSplinePoints();

	// Two points is the least a road can be built from, so this refuses rather than silently
	// leaving a road that cannot generate a mesh.
	if (Num <= 2 || SelectedPointIndex < 0 || SelectedPointIndex >= Num) return false;

	RecordUndo(Road);

	TArray<FVector> Points;
	Points.Reserve(Num - 1);
	for (int32 i = 0; i < Num; ++i)
	{
		if (i != SelectedPointIndex)
		{
			Points.Add(Road->GetSplinePointWorldLocation(i));
		}
	}

	Road->SetPointsRealtime(Points);
	Road->RebuildRoad();
	Road->RefreshAffectedJunctions();

	SelectedPointIndex = FMath::Clamp(SelectedPointIndex, 0, Points.Num() - 1);
	OverlayBuiltForRoad = nullptr;
	return true;
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
	// A drag can still be in progress when selection is cleared out from under it (switching tool
	// mode mid-drag, a road being deleted while held, etc). Settle and reset the drag flags first,
	// the same way a normal mouse-up would -- otherwise bIsDraggingPoint stays stuck true, and the
	// very next point the player selects starts "dragging" on its own the instant TickComponent
	// next runs, with no press involved. EndPointDrag no-ops harmlessly if nothing was dragging.
	EndPointDrag();

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
		// PointSelectionRadius reaches noticeably farther than a click needs to precisely hit a
		// point handle, so a click meant to extend the road can land inside that radius around the
		// point you're extending FROM and silently re-select it instead -- the road just doesn't
		// grow, with nothing on screen explaining why. Flag exactly that case (re-clicking the
		// point that's already selected) rather than every reselect, since picking a *different*
		// existing point this way is normal, intended behaviour.
		if (GEngine && HitRoad == SelectedRoadActor.Get() && HitPointIndex == SelectedPointIndex)
		{
			GEngine->AddOnScreenDebugMessage(
				(uint64)GetUniqueID(), 1.5f, FColor::Yellow,
				TEXT("Too close to the selected point to extend -- click farther away."));
		}

		SelectRoadPoint(HitRoad, HitPointIndex);
		return;
	}

	// A click on a road's line, away from its points, anchors to the nearest point that road
	// already has. Inserting one here instead meant that simply clicking a road to work with it
	// silently added geometry to it.
	if (!SelectedRoadActor.IsValid())
	{
		ARealtimeRoadSplineActor* LineRoad = nullptr;
		float LineDistance = 0.0f;
		if (FindRoadLineNear(ClickedWorldLocation, LineRoad, LineDistance))
		{
			float Unused = 0.0f;
			const int32 Nearest = LineRoad->FindClosestSplinePoint(ClickedWorldLocation, Unused);
			if (Nearest != INDEX_NONE)
			{
				SelectRoadPoint(LineRoad, Nearest);
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

		RecordUndo(Road);

		// ExtendFromPointToLocation is the only place that actually knows which end grew (or
		// whether an interior point branched into a whole new road) -- ask it, rather than
		// re-deriving "which end" here from stale state, which is what used to desync the
		// highlighted point from the one the user just placed.
		int32 NewSelectedIndex = INDEX_NONE;
		ARealtimeRoadSplineActor* ActiveRoad = Road->ExtendFromPointToLocation(SelectedPointIndex, Destination, NewSelectedIndex, AutoPointSpacing);

		if (ActiveRoad && NewSelectedIndex != INDEX_NONE)
		{
			if (SnappedJunction)
			{
				// Record the attachment so the road's end stays pinned to the leg socket on every
				// later rebuild, not just at the moment of the click. Applies to whichever road
				// actually grew -- the branch road when extending from an interior point, not the
				// unchanged original.
				const bool bGrewFromStart = (NewSelectedIndex == 0);
				if (bGrewFromStart)
				{
					ActiveRoad->StartJunction = SnappedJunction;
					ActiveRoad->StartJunctionLegIndex = SnappedLeg;
				}
				else
				{
					ActiveRoad->EndJunction = SnappedJunction;
					ActiveRoad->EndJunctionLegIndex = SnappedLeg;
				}
				ActiveRoad->RebuildRoad();
			}

			// Follow the point that just grew, so consecutive clicks keep extending in the same
			// direction and the overlay highlights the point the user is actually looking at.
			SelectRoadPoint(ActiveRoad, NewSelectedIndex);
		}

		OnRoadPlaced.Broadcast(ActiveRoad ? ActiveRoad : Road);
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

	// Clicking the road's line selects the nearest point it already has. It deliberately does not
	// insert one: a click meant to pick something should never quietly change it, and adding
	// geometry is an edit that belongs behind a deliberate action.
	float Along = 0.0f;
	if (FindRoadLineNear(WorldLocation, Road, Along))
	{
		float Unused = 0.0f;
		const int32 Nearest = Road->FindClosestSplinePoint(WorldLocation, Unused);
		if (Nearest != INDEX_NONE)
		{
			SelectRoadPoint(Road, Nearest);
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
