// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.
#include "IO/TerraDyneDEMMeshBuilder.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/MeshNormals.h"

void FTerraDyneDEMMeshBuilder::BuildDEMMesh(
	UE::Geometry::FDynamicMesh3& TargetMesh,
	const TArray<float>& HeightBuffer,
	int32 Resolution,
	float WorldSize,
	float ZScale)
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

	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh.Attributes()->PrimaryUV();
	UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = TargetMesh.Attributes()->PrimaryNormals();

	const double Step = (double)WorldSize / (double)(Resolution - 1);
	const double Offset = (double)WorldSize * -0.5;

	// Append vertices and normalized UVs
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const double PY = Offset + (Y * Step);
		const float V = (float)Y / (float)(Resolution - 1);

		for (int32 X = 0; X < Resolution; ++X)
		{
			const double PX = Offset + (X * Step);
			const float U = (float)X / (float)(Resolution - 1);

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
		}
	}

	// Fast normal computation
	UE::Geometry::FMeshNormals::QuickComputeVertexNormals(TargetMesh);
	if (NormalOverlay)
	{
		UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
	}
}

void FTerraDyneDEMMeshBuilder::UpdateComponentMesh(
	UDynamicMeshComponent* MeshComp,
	const TArray<float>& HeightBuffer,
	int32 Resolution,
	float WorldSize,
	float ZScale,
	bool bComputeCollision)
{
	if (!MeshComp || !MeshComp->GetDynamicMesh())
	{
		return;
	}

	MeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		BuildDEMMesh(Mesh, HeightBuffer, Resolution, WorldSize, ZScale);
	});

	MeshComp->SetCastShadow(true);
	MeshComp->SetCullDistance(0.0f);

	MeshComp->NotifyMeshUpdated();

	if (bComputeCollision)
	{
		MeshComp->UpdateCollision(true);
	}
}
