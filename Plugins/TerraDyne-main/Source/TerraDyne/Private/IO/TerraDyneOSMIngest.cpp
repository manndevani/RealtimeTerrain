// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneOSMIngest.h"
#include "Algo/Reverse.h"

namespace
{
	/** Two corners closer than this are the same corner as far as a footprint is concerned. */
	constexpr double DuplicatePointToleranceSq = 1.0; // 1 cm^2

	TArray<FVector> DropConsecutiveDuplicates(const TArray<FVector>& Points)
	{
		TArray<FVector> Cleaned;
		Cleaned.Reserve(Points.Num());
		for (const FVector& P : Points)
		{
			if (Cleaned.Num() == 0 || FVector::DistSquaredXY(Cleaned.Last(), P) > DuplicatePointToleranceSq)
			{
				Cleaned.Add(P);
			}
		}
		return Cleaned;
	}
}

FString FTerraDyneOSMImportSummary::ToDisplayString() const
{
	if (!bSuccess)
	{
		return ErrorMessage.IsEmpty() ? TEXT("OSM import failed.") : ErrorMessage;
	}

	FString Text = FString::Printf(TEXT("%d buildings, %d roads"), BuildingsCreated, RoadsCreated);
	if (BuildingsSkipped > 0 || RoadsSkipped > 0)
	{
		Text += FString::Printf(TEXT("  (skipped %d/%d unusable)"), BuildingsSkipped, RoadsSkipped);
	}
	if (DroppedToLimit > 0)
	{
		Text += FString::Printf(TEXT("  -- truncated, %d more not imported"), DroppedToLimit);
	}
	return Text;
}

double FTerraDyneOSMIngest::ComputeSignedArea2D(const TArray<FVector>& Points)
{
	if (Points.Num() < 3)
	{
		return 0.0;
	}

	double Twice = 0.0;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const FVector& A = Points[i];
		const FVector& B = Points[(i + 1) % Points.Num()];
		Twice += (A.X * B.Y) - (B.X * A.Y);
	}
	return Twice * 0.5;
}

void FTerraDyneOSMIngest::NormalizeFootprintWinding(TArray<FVector>& Points)
{
	if (ComputeSignedArea2D(Points) < 0.0)
	{
		Algo::Reverse(Points);
	}
}

TArray<FVector> FTerraDyneOSMIngest::BuildFootprintLocal(const TArray<FVector>& WorldPoints, FVector& OutOrigin)
{
	OutOrigin = FVector::ZeroVector;

	TArray<FVector> Flat;
	Flat.Reserve(WorldPoints.Num());
	for (const FVector& P : WorldPoints)
	{
		Flat.Add(FVector(P.X, P.Y, 0.0));
	}

	// OSM closes a ring by repeating its first node; the footprint spline is already a closed loop
	// and a duplicated corner would give the building a zero-length wall segment.
	if (Flat.Num() >= 2 && FVector::DistSquaredXY(Flat[0], Flat.Last()) <= DuplicatePointToleranceSq)
	{
		Flat.Pop();
	}

	Flat = DropConsecutiveDuplicates(Flat);
	if (Flat.Num() < 3)
	{
		return TArray<FVector>();
	}

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& P : Flat)
	{
		Centroid += P;
	}
	Centroid /= static_cast<double>(Flat.Num());
	OutOrigin = Centroid;

	TArray<FVector> Local;
	Local.Reserve(Flat.Num());
	for (const FVector& P : Flat)
	{
		Local.Add(FVector(P.X - Centroid.X, P.Y - Centroid.Y, 0.0));
	}

	NormalizeFootprintWinding(Local);
	return Local;
}

TArray<FVector> FTerraDyneOSMIngest::BuildRoadCentreline(const TArray<FVector>& WorldPoints)
{
	TArray<FVector> Cleaned = DropConsecutiveDuplicates(WorldPoints);

	// A closed way used as a road (a roundabout, a cul-de-sac loop) still repeats its first node.
	// Keeping it would leave a zero-length final segment for the mesh builder to divide by.
	if (Cleaned.Num() >= 3 && FVector::DistSquaredXY(Cleaned[0], Cleaned.Last()) <= DuplicatePointToleranceSq)
	{
		Cleaned.Pop();
	}

	return Cleaned;
}

bool FTerraDyneOSMIngest::ResolveWayWorldPoints(
	const FTerraDyneOSMData& Data,
	const FTerraDyneOSMWay& Way,
	TFunctionRef<FVector(double, double)> LonLatToWorld,
	TArray<FVector>& OutWorldPoints)
{
	OutWorldPoints.Reset();
	OutWorldPoints.Reserve(Way.NodeIds.Num());

	for (const uint64 NodeId : Way.NodeIds)
	{
		const FTerraDyneOSMNode* Node = Data.Nodes.Find(NodeId);
		if (!Node)
		{
			// A partial extract routinely references nodes outside its own bounding box. Reject the
			// whole way rather than building a shape with a corner quietly missing.
			return false;
		}
		OutWorldPoints.Add(LonLatToWorld(Node->Lon, Node->Lat));
	}

	return true;
}
