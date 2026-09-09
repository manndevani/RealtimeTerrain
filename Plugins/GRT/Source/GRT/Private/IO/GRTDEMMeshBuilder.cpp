// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
#include "IO/GRTDEMMeshBuilder.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/MeshNormals.h"

namespace
{
	// Natural topographic hypsometric elevation tint (rich natural earth tones)
	FVector4f EvaluateHypsometricElevationColor(float H)
	{
		H = FMath::Clamp(H, 0.0f, 1.0f);
		if (H < 0.20f)
		{
			// Low valley & river basin: Deep Emerald Forest Green
			const float T = H / 0.20f;
			const FLinearColor C = FMath::Lerp(FLinearColor(0.04f, 0.22f, 0.06f), FLinearColor(0.12f, 0.42f, 0.10f), T);
			return FVector4f(C.R, C.G, C.B, 1.0f);
		}
		else if (H < 0.40f)
		{
			// Lowlands & Foothills: Vibrant Grassland -> Golden Olive
			const float T = (H - 0.20f) / 0.20f;
			const FLinearColor C = FMath::Lerp(FLinearColor(0.12f, 0.42f, 0.10f), FLinearColor(0.48f, 0.45f, 0.12f), T);
			return FVector4f(C.R, C.G, C.B, 1.0f);
		}
		else if (H < 0.60f)
		{
			// Highlands: Golden Olive -> Warm Terracotta & Canyon Ochre
			const float T = (H - 0.40f) / 0.20f;
			const FLinearColor C = FMath::Lerp(FLinearColor(0.48f, 0.45f, 0.12f), FLinearColor(0.55f, 0.30f, 0.10f), T);
			return FVector4f(C.R, C.G, C.B, 1.0f);
		}
		else if (H < 0.78f)
		{
			// Subalpine Mountain Rock: Canyon Ochre -> Mountain Slate Granite
			const float T = (H - 0.60f) / 0.18f;
			const FLinearColor C = FMath::Lerp(FLinearColor(0.55f, 0.30f, 0.10f), FLinearColor(0.28f, 0.26f, 0.28f), T);
			return FVector4f(C.R, C.G, C.B, 1.0f);
		}
		else if (H < 0.90f)
		{
			// High Alpine Rock: Slate Granite -> Cold High Altitude Slate
			const float T = (H - 0.78f) / 0.12f;
			const FLinearColor C = FMath::Lerp(FLinearColor(0.28f, 0.26f, 0.28f), FLinearColor(0.48f, 0.50f, 0.55f), T);
			return FVector4f(C.R, C.G, C.B, 1.0f);
		}
		else
		{
			// Alpine Snow Peak
			const float T = (H - 0.90f) / 0.10f;
			const FLinearColor C = FMath::Lerp(FLinearColor(0.48f, 0.50f, 0.55f), FLinearColor(0.92f, 0.95f, 1.00f), T);
			return FVector4f(C.R, C.G, C.B, 1.0f);
		}
	}

	FVector4f SampleColorAtWorldCoord(
		double WorldX, double WorldY, float NormHeight,
		bool bUseGlobalUVs, FVector2D GlobalFootprintMin, FVector2D GlobalFootprintMax,
		const TArray<FColor>& OverlayPixels, int32 OverlayWidth, int32 OverlayHeight)
	{
		if (OverlayPixels.Num() > 0 && OverlayWidth > 0 && OverlayHeight > 0 && bUseGlobalUVs)
		{
			const double SpanX = FMath::Max(1.0, GlobalFootprintMax.X - GlobalFootprintMin.X);
			const double SpanY = FMath::Max(1.0, GlobalFootprintMax.Y - GlobalFootprintMin.Y);
			// In Cesium coordinate space for this project:
			// WorldX is Easting (West -> East: Col 0 -> Col Max)
			// WorldY is Northing/Southing (North -> South: Row 0 -> Row Max)
			const double U = FMath::Clamp((WorldX - GlobalFootprintMin.X) / SpanX, 0.0, 1.0);
			const double V = FMath::Clamp((WorldY - GlobalFootprintMin.Y) / SpanY, 0.0, 1.0);

			const int32 PX = FMath::Clamp((int32)FMath::RoundToDouble(U * (OverlayWidth - 1)), 0, OverlayWidth - 1);
			const int32 PY = FMath::Clamp((int32)FMath::RoundToDouble(V * (OverlayHeight - 1)), 0, OverlayHeight - 1);
			const int32 PixelIdx = PY * OverlayWidth + PX;

			if (OverlayPixels.IsValidIndex(PixelIdx))
			{
				const FColor C = OverlayPixels[PixelIdx];
				const FLinearColor LinC = FLinearColor::FromSRGBColor(C);
				return FVector4f(LinC.R, LinC.G, LinC.B, LinC.A);
			}
		}

		return EvaluateHypsometricElevationColor(NormHeight);
	}
}

void FGRTDEMMeshBuilder::BuildDEMMesh(
	UE::Geometry::FDynamicMesh3& TargetMesh,
	const TArray<float>& HeightBuffer,
	int32 Resolution,
	float WorldSize,
	float ZScale,
	FVector ChunkLocation,
	FVector2D GlobalFootprintMin,
	FVector2D GlobalFootprintMax,
	const TArray<FColor>& OverlayPixels,
	int32 OverlayWidth,
	int32 OverlayHeight)
{
	if (Resolution < 2)
	{
		return;
	}

	const int32 ExpectedSamples = Resolution * Resolution;
	if (HeightBuffer.Num() < ExpectedSamples)
	{
		return;
	}

	TargetMesh.Clear();
	TargetMesh.EnableAttributes();
	TargetMesh.Attributes()->EnablePrimaryColors();

	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh.Attributes()->PrimaryUV();
	UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = TargetMesh.Attributes()->PrimaryNormals();
	UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = TargetMesh.Attributes()->PrimaryColors();

	const double Step = (double)WorldSize / (double)(Resolution - 1);
	const double Offset = (double)WorldSize * -0.5;

	const bool bUseGlobalUVs = (GlobalFootprintMax.X > GlobalFootprintMin.X) && (GlobalFootprintMax.Y > GlobalFootprintMin.Y);
	const double FootprintSpanX = bUseGlobalUVs ? (GlobalFootprintMax.X - GlobalFootprintMin.X) : 1.0;
	const double FootprintSpanY = bUseGlobalUVs ? (GlobalFootprintMax.Y - GlobalFootprintMin.Y) : 1.0;

	// Append vertices, normalized UVs, and vertex colors
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const double PY = Offset + (Y * Step);
		for (int32 X = 0; X < Resolution; ++X)
		{
			const double PX = Offset + (X * Step);
			const double WorldX = ChunkLocation.X + PX;
			const double WorldY = ChunkLocation.Y + PY;

			float U, V;
			if (bUseGlobalUVs)
			{
				U = (float)FMath::Clamp((WorldX - GlobalFootprintMin.X) / FootprintSpanX, 0.0, 1.0);
				V = (float)FMath::Clamp((WorldY - GlobalFootprintMin.Y) / FootprintSpanY, 0.0, 1.0);
			}
			else
			{
				U = (float)X / (float)(Resolution - 1);
				V = (float)Y / (float)(Resolution - 1);
			}

			float Height = HeightBuffer[Y * Resolution + X];
			if (!FMath::IsFinite(Height))
			{
				Height = 0.0f;
			}

			TargetMesh.AppendVertex(FVector3d(PX, PY, (double)Height * (double)ZScale));
			if (UVOverlay)
			{
				UVOverlay->AppendElement(FVector2f(U, V));
			}

			if (ColorOverlay)
			{
				const FVector4f VColor = SampleColorAtWorldCoord(
					WorldX, WorldY, Height,
					bUseGlobalUVs, GlobalFootprintMin, GlobalFootprintMax,
					OverlayPixels, OverlayWidth, OverlayHeight);
				ColorOverlay->AppendElement(VColor);
			}
		}
	}

	// Triangulate grid
	for (int32 Y = 0; Y < Resolution - 1; ++Y)
	{
		for (int32 X = 0; X < Resolution - 1; ++X)
		{
			const int32 BL = Y * Resolution + X;
			const int32 BR = BL + 1;
			const int32 TL = (Y + 1) * Resolution + X;
			const int32 TR = TL + 1;

			const int32 T0 = TargetMesh.AppendTriangle(BL, TR, BR);
			const int32 T1 = TargetMesh.AppendTriangle(BL, TL, TR);

			if (UVOverlay)
			{
				if (T0 >= 0) UVOverlay->SetTriangle(T0, UE::Geometry::FIndex3i(BL, TR, BR));
				if (T1 >= 0) UVOverlay->SetTriangle(T1, UE::Geometry::FIndex3i(BL, TL, TR));
			}

			if (ColorOverlay)
			{
				if (T0 >= 0) ColorOverlay->SetTriangle(T0, UE::Geometry::FIndex3i(BL, TR, BR));
				if (T1 >= 0) ColorOverlay->SetTriangle(T1, UE::Geometry::FIndex3i(BL, TL, TR));
			}
		}
	}

	// Fast normal computation
	UE::Geometry::FMeshNormals::QuickComputeVertexNormals(TargetMesh);
	if (NormalOverlay)
	{
		UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
	}
}

void FGRTDEMMeshBuilder::UpdateComponentMesh(
	UDynamicMeshComponent* MeshComp,
	const TArray<float>& HeightBuffer,
	int32 Resolution,
	float WorldSize,
	float ZScale,
	bool bComputeCollision,
	FVector ChunkLocation,
	FVector2D GlobalFootprintMin,
	FVector2D GlobalFootprintMax,
	const TArray<FColor>& OverlayPixels,
	int32 OverlayWidth,
	int32 OverlayHeight)
{
	if (!MeshComp || !MeshComp->GetDynamicMesh())
	{
		return;
	}

	MeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		BuildDEMMesh(
			Mesh,
			HeightBuffer,
			Resolution,
			WorldSize,
			ZScale,
			ChunkLocation,
			GlobalFootprintMin,
			GlobalFootprintMax,
			OverlayPixels,
			OverlayWidth,
			OverlayHeight);
	});

	MeshComp->SetColorOverrideMode(EDynamicMeshComponentColorOverrideMode::VertexColors);
	MeshComp->SetCastShadow(true);
	MeshComp->SetCullDistance(0.0f);

	MeshComp->NotifyMeshUpdated();

	if (bComputeCollision)
	{
		MeshComp->UpdateCollision(true);
	}
}

void FGRTDEMMeshBuilder::UpdateDEMVertexColors(
	UE::Geometry::FDynamicMesh3& TargetMesh,
	const TArray<float>& HeightBuffer,
	int32 Resolution,
	float WorldSize,
	FVector ChunkLocation,
	FVector2D GlobalFootprintMin,
	FVector2D GlobalFootprintMax,
	const TArray<FColor>& OverlayPixels,
	int32 OverlayWidth,
	int32 OverlayHeight)
{
	const int32 Count = Resolution * Resolution;
	if (Count <= 0 || HeightBuffer.Num() < Count || TargetMesh.VertexCount() != Count)
	{
		return;
	}

	TargetMesh.Attributes()->EnablePrimaryColors();
	UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = TargetMesh.Attributes()->PrimaryColors();
	if (!ColorOverlay)
	{
		return;
	}

	const double Step = (double)WorldSize / (double)(Resolution - 1);
	const double Offset = (double)WorldSize * -0.5;
	const bool bUseGlobalUVs = (GlobalFootprintMax.X > GlobalFootprintMin.X) && (GlobalFootprintMax.Y > GlobalFootprintMin.Y);

	const bool bReuse = (ColorOverlay->ElementCount() == Count);
	if (!bReuse)
	{
		ColorOverlay->ClearElements();
	}

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const double PY = Offset + (Y * Step);
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 VID = Y * Resolution + X;
			const double PX = Offset + (X * Step);
			const double WorldX = ChunkLocation.X + PX;
			const double WorldY = ChunkLocation.Y + PY;
			const float Height = HeightBuffer[VID];

			const FVector4f VColor = SampleColorAtWorldCoord(
				WorldX, WorldY, Height,
				bUseGlobalUVs, GlobalFootprintMin, GlobalFootprintMax,
				OverlayPixels, OverlayWidth, OverlayHeight);

			if (bReuse)
			{
				ColorOverlay->SetElement(VID, VColor);
			}
			else
			{
				ColorOverlay->AppendElement(VColor);
			}
		}
	}

	if (!bReuse)
	{
		for (int32 TID : TargetMesh.TriangleIndicesItr())
		{
			ColorOverlay->SetTriangle(TID, TargetMesh.GetTriangle(TID));
		}
	}
}
