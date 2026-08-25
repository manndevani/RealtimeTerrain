// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "RoadMeshBuilder.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "IndexTypes.h"
#include "CompGeom/PolygonTriangulation.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "Core/TerraDyneManager.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FDynamicMeshUVOverlay;
using UE::Geometry::FDynamicMeshMaterialAttribute;
using UE::Geometry::FIndex3i;

namespace RoadMesh
{
namespace
{
	/**
	 * Appends one triangle with its material ID and UVs.
	 *
	 * A fresh UV element is appended per triangle corner rather than shared between neighbours.
	 * That costs elements but removes a whole class of seam bugs, and normals are split downstream
	 * anyway. FDynamicMesh3::AppendTriangle returns a negative sentinel for non-manifold and
	 * duplicate cases as well as InvalidID, so the result is tested against zero, not against
	 * InvalidID alone.
	 */
	int32 AppendTri(
		FDynamicMesh3& Mesh,
		int32 A, int32 B, int32 C,
		int32 MaterialID,
		const FVector2f& UVA, const FVector2f& UVB, const FVector2f& UVC)
	{
		// Winding is reversed here, once, rather than at every call site.
		//
		// Callers order their vertices by the standard right-handed rule, where the front face of
		// (A, B, C) is (B-A) x (C-A). GeometryCore does not follow that: VectorUtil::Normal
		// computes edge2 x edge1, commented in the engine as "Unreal has Left-Hand Coordinate
		// system, so we need to reverse cross product". Emitting the standard order therefore
		// produced a mesh that was entirely inside out, with a negative surface area.
		const int32 Tid = Mesh.AppendTriangle(FIndex3i(A, C, B), MaterialID);
		if (Tid < 0)
		{
			return INDEX_NONE;
		}

		if (FDynamicMeshMaterialAttribute* MatAttrib = Mesh.Attributes()->GetMaterialID())
		{
			MatAttrib->SetValue(Tid, MaterialID);
		}

		if (FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV())
		{
			const int32 E0 = UVOverlay->AppendElement(UVA);
			const int32 E1 = UVOverlay->AppendElement(UVB);
			const int32 E2 = UVOverlay->AppendElement(UVC);
			// Element order has to track the swapped vertex order above.
			UVOverlay->SetTriangle(Tid, FIndex3i(E0, E2, E1));
		}

		return Tid;
	}
}

void PrepareMesh(FDynamicMesh3& Mesh)
{
	Mesh.EnableTriangleGroups();
	Mesh.EnableAttributes();

	// EnableAttributes creates a UV layer and a normal layer but never a material ID attribute.
	// Without this call Attributes()->GetMaterialID() returns null and every triangle collapses
	// onto material slot 0.
	Mesh.Attributes()->EnableMaterialID();

	if (Mesh.Attributes()->NumUVLayers() < 1)
	{
		Mesh.Attributes()->SetNumUVLayers(1);
	}
}

void BuildRoadSection(const FRoadCrossSectionSettings& Settings, bool bIncludeKerbAndPath, FSection& OutSection)
{
	OutSection.Reset();

	const double Half = FMath::Max(50.0f, Settings.RoadWidth) * 0.5;
	const bool bCurbs = bIncludeKerbAndPath && Settings.bHasCurbs;
	const bool bPaths = bIncludeKerbAndPath && Settings.bHasFootpaths;

	const double CurbW = bCurbs ? FMath::Max(0.0f, Settings.CurbWidth) : 0.0;
	const double CurbH = bCurbs ? FMath::Max(0.0f, Settings.CurbHeight) : 0.0;
	const double PathW = bPaths ? FMath::Max(0.0f, Settings.FootpathWidth) : 0.0;
	const double PathH = bPaths ? FMath::Max(0.0f, Settings.FootpathHeight) : 0.0;

	// The crown sits at zero and both edges drop, so the spline still runs along the surface.
	const double Crown = Half * FMath::Max(0.0f, Settings.CamberPercent) * 0.01;
	const double Outer = Half + CurbW + PathW;
	const double Base = -Crown - FMath::Max(2.0f, Settings.RoadThickness);

	// The face rising off the outer edge of the drive surface is curb stone when there is a curb
	// and plain slab flank when there is not. The two share a slot today; naming both keeps the
	// intent readable if they are ever separated.
	const int32 EdgeRiserMaterial = bCurbs ? Slots::Curb : Slots::Flank;

	// Top chain, left to right.
	if (bPaths)
	{
		OutSection.Emplace(-Outer, PathH, Slots::Footpath);
		OutSection.Emplace(-(Half + CurbW), PathH, Slots::Curb);
	}
	if (bCurbs)
	{
		OutSection.Emplace(-(Half + CurbW), CurbH, Slots::Curb);
		OutSection.Emplace(-Half, CurbH, Slots::Curb);
	}

	OutSection.Emplace(-Half, -Crown, Slots::Surface);
	OutSection.Emplace(0.0, 0.0, Slots::Surface);
	OutSection.Emplace(Half, -Crown, EdgeRiserMaterial);

	if (bCurbs)
	{
		OutSection.Emplace(Half, CurbH, Slots::Curb);
		OutSection.Emplace(Half + CurbW, CurbH, bPaths ? Slots::Curb : Slots::Flank);
	}
	if (bPaths)
	{
		OutSection.Emplace(Half + CurbW, PathH, Slots::Footpath);
		OutSection.Emplace(Outer, PathH, Slots::Flank);
	}

	// Underside, right to left, closing the loop back to the first vertex.
	OutSection.Emplace(Outer, Base, Slots::Flank);
	OutSection.Emplace(-Outer, Base, Slots::Flank);
}

void BuildRoadsideSection(
	double CurbWidth,
	double CurbHeight,
	double FootpathWidth,
	double FootpathHeight,
	double Thickness,
	FSection& OutSection)
{
	OutSection.Reset();

	const double CurbW = FMath::Max(0.0, CurbWidth);
	const double CurbH = FMath::Max(0.0, CurbHeight);
	const double PathW = FMath::Max(0.0, FootpathWidth);
	const double PathH = FMath::Max(0.0, FootpathHeight);
	const double Outer = CurbW + PathW;
	const double Base = -FMath::Max(2.0, Thickness);

	if (Outer <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	OutSection.Emplace(0.0, 0.0, Slots::Curb);
	OutSection.Emplace(0.0, CurbH, Slots::Curb);
	OutSection.Emplace(CurbW, CurbH, Slots::Curb);
	OutSection.Emplace(CurbW, PathH, Slots::Footpath);
	OutSection.Emplace(Outer, PathH, Slots::Flank);
	OutSection.Emplace(Outer, Base, Slots::Flank);

	// Open strip: the final vertex closes nothing, so its edge material is never read.
	OutSection.Emplace(0.0, Base, Slots::Flank);
}

bool BuildFramesFromPolyline(const TArray<FVector>& Points, TArray<FFrame>& OutFrames)
{
	OutFrames.Reset();
	if (Points.Num() < 2)
	{
		return false;
	}

	// Coincident points produce a zero tangent and poison the whole frame.
	TArray<FVector> Clean;
	Clean.Reserve(Points.Num());
	for (const FVector& Pt : Points)
	{
		if (Pt.ContainsNaN())
		{
			continue;
		}
		if (Clean.Num() == 0 || FVector::DistSquared(Clean.Last(), Pt) > 1.0)
		{
			Clean.Add(Pt);
		}
	}
	if (Clean.Num() < 2)
	{
		return false;
	}

	const int32 Num = Clean.Num();
	OutFrames.SetNum(Num);

	double Travelled = 0.0;
	for (int32 i = 0; i < Num; ++i)
	{
		FFrame& Frame = OutFrames[i];
		Frame.Origin = Clean[i];

		if (i > 0)
		{
			Travelled += FVector::Dist(Clean[i - 1], Clean[i]);
		}
		Frame.DistanceAlong = Travelled;

		// Central difference in the interior, one-sided at the ends.
		FVector Tangent;
		if (i == 0)
		{
			Tangent = Clean[1] - Clean[0];
		}
		else if (i == Num - 1)
		{
			Tangent = Clean[i] - Clean[i - 1];
		}
		else
		{
			Tangent = Clean[i + 1] - Clean[i - 1];
		}

		Frame.Tangent = Tangent.GetSafeNormal();
		if (Frame.Tangent.IsNearlyZero())
		{
			Frame.Tangent = FVector::ForwardVector;
		}

		if (i == 0)
		{
			// Right comes from world up, not from any spline up vector, so the section stays
			// upright no matter how the source spline is twisted. This only seeds the very first
			// frame -- every later station propagates from it (see below) rather than re-deriving
			// from world up independently, so there is no discontinuity left to hit if the road
			// climbs through vertical partway along its length.
			FVector Right = FVector::CrossProduct(FVector::UpVector, Frame.Tangent);
			if (Right.SizeSquared() < UE_KINDA_SMALL_NUMBER)
			{
				// The first tangent alone is near-vertical: any horizontal axis is as good as
				// another to seed the propagation with.
				Right = FVector::CrossProduct(FVector::ForwardVector, Frame.Tangent);
			}
			Frame.Right = Right.GetSafeNormal();
		}
		else
		{
			// Rotation-minimizing frame, carried forward from the previous station via the double
			// reflection method (Wang, Jüttler, Zheng & Liu, 2008). Re-deriving Right from world up
			// at every station -- the previous approach -- has a hard singularity at a vertical
			// tangent: crossing it forces a one-station cut to a differently-oriented fallback axis,
			// which is exactly the 180-degree section twist seen on steep grades and switchbacks.
			// Propagating instead has no singularity at any tangent direction, including straight
			// up, because it only ever asks "how do I carry the previous frame forward," never
			// "what does a fixed reference vector say from scratch." Deliberate banking is layered
			// on top afterwards by ApplyRoll and is unaffected by how this base frame is derived.
			const FFrame& Prev = OutFrames[i - 1];

			const FVector V1 = Frame.Origin - Prev.Origin;
			const double C1 = FVector::DotProduct(V1, V1);
			FVector ReflectedRight = Prev.Right;
			FVector ReflectedTangent = Prev.Tangent;
			if (C1 > UE_KINDA_SMALL_NUMBER)
			{
				ReflectedRight = Prev.Right - (2.0 / C1) * FVector::DotProduct(V1, Prev.Right) * V1;
				ReflectedTangent = Prev.Tangent - (2.0 / C1) * FVector::DotProduct(V1, Prev.Tangent) * V1;
			}

			const FVector V2 = Frame.Tangent - ReflectedTangent;
			const double C2 = FVector::DotProduct(V2, V2);
			FVector Right = ReflectedRight;
			if (C2 > UE_KINDA_SMALL_NUMBER)
			{
				Right = ReflectedRight - (2.0 / C2) * FVector::DotProduct(V2, ReflectedRight) * V2;
			}
			Frame.Right = Right.GetSafeNormal();
		}

		Frame.Up = FVector::CrossProduct(Frame.Tangent, Frame.Right).GetSafeNormal();
	}

	return true;
}

void ApplyRoll(TArray<FFrame>& Frames, const TArray<double>& RollRadians)
{
	const int32 Num = FMath::Min(Frames.Num(), RollRadians.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		const double Angle = RollRadians[i];
		if (FMath::IsNearlyZero(Angle))
		{
			continue;
		}

		const FQuat Rotation(Frames[i].Tangent, Angle);
		Frames[i].Right = Rotation.RotateVector(Frames[i].Right);
		Frames[i].Up = Rotation.RotateVector(Frames[i].Up);
	}
}

void ComputeBandScaleFromCurvature(
	const TArray<FFrame>& Frames,
	double RequestedBandWidth,
	double SafetyFactor,
	TArray<float>& OutBandScale)
{
	const int32 Num = Frames.Num();
	OutBandScale.Init(1.0f, Num);

	if (RequestedBandWidth <= UE_KINDA_SMALL_NUMBER || Num < 3)
	{
		// Nothing to shrink against, or too few stations to have an interior curvature sample at
		// all -- every frame keeps the default full-width scale.
		return;
	}

	const double SafeSafetyFactor = FMath::Max(1.0, SafetyFactor);

	// Curvature at an interior station, from how much the tangent turns between its neighbours over
	// the distance between them -- the same central-difference spirit BuildFramesFromPolyline
	// already uses for the tangent itself, just one derivative further. Untouched at the two
	// endpoints: a swept road's terminal stations are typically a straight cap or a junction
	// transition, not the tight part of a curve, so leaving them at full scale costs nothing and
	// avoids a one-sided (and so noisier) curvature estimate right at the ends.
	for (int32 i = 1; i < Num - 1; ++i)
	{
		const double SegmentLength = Frames[i + 1].DistanceAlong - Frames[i - 1].DistanceAlong;
		if (SegmentLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const double CosTurn = FMath::Clamp(FVector::DotProduct(Frames[i - 1].Tangent, Frames[i + 1].Tangent), -1.0, 1.0);
		const double TurnAngle = FMath::Acos(CosTurn);
		if (TurnAngle <= UE_KINDA_SMALL_NUMBER)
		{
			// Locally straight: no curvature, no limit on how wide a band this station can carry.
			continue;
		}

		const double LocalRadius = SegmentLength / TurnAngle;
		const double MaxSafeBand = LocalRadius / SafeSafetyFactor;
		OutBandScale[i] = static_cast<float>(FMath::Clamp(MaxSafeBand / RequestedBandWidth, 0.0, 1.0));
	}
}

void SweepSection(
	FDynamicMesh3& Mesh,
	const FSection& Section,
	const TArray<FFrame>& Frames,
	const FTransform& WorldToLocal,
	const FSweepOptions& Options)
{
	const int32 NumSection = Section.Num();
	const int32 NumFrames = Frames.Num();
	if (NumSection < 2 || NumFrames < 2)
	{
		return;
	}

	PrepareMesh(Mesh);

	// Cumulative arc length around the section drives U, so texel density matches the V axis.
	TArray<double> SectionU;
	SectionU.SetNumZeroed(NumSection + 1);
	for (int32 i = 1; i <= NumSection; ++i)
	{
		const FSectionVertex& Prev = Section[i - 1];
		const FSectionVertex& Curr = Section[i % NumSection];
		SectionU[i] = SectionU[i - 1] + FMath::Sqrt(
			FMath::Square(Curr.Lateral - Prev.Lateral) + FMath::Square(Curr.Vertical - Prev.Vertical));
	}

	TArray<int32> Verts;
	Verts.SetNum(NumFrames * NumSection);

	for (int32 f = 0; f < NumFrames; ++f)
	{
		const FFrame& Frame = Frames[f];
		const float BandScale = Options.BandScalePerFrame.IsValidIndex(f) ? Options.BandScalePerFrame[f] : 1.0f;

		for (int32 s = 0; s < NumSection; ++s)
		{
			double Lateral = Section[s].Lateral;

			// Only the outer band -- curb, footpath -- ever scales. The core road surface, inside
			// BandInnerHalfWidth, stays at its authored width at every station regardless: a curve
			// tight enough to need a narrower band is not tight enough to need a narrower road.
			if (Options.BandInnerHalfWidth > 0.0)
			{
				const double AbsLateral = FMath::Abs(Lateral);
				if (AbsLateral > Options.BandInnerHalfWidth)
				{
					const double BandDepth = AbsLateral - Options.BandInnerHalfWidth;
					Lateral = FMath::Sign(Lateral) * (Options.BandInnerHalfWidth + BandDepth * BandScale);
				}
			}

			const FVector WorldPos = Frame.Origin + Frame.Right * Lateral + Frame.Up * Section[s].Vertical;
			Verts[f * NumSection + s] = Mesh.AppendVertex(WorldToLocal.TransformPosition(WorldPos));
		}
	}

	const int32 NumEdges = Options.bSectionIsClosed ? NumSection : NumSection - 1;

	for (int32 f = 0; f + 1 < NumFrames; ++f)
	{
		const float VNear = static_cast<float>(Frames[f].DistanceAlong * Options.UVScale);
		const float VFar = static_cast<float>(Frames[f + 1].DistanceAlong * Options.UVScale);

		for (int32 e = 0; e < NumEdges; ++e)
		{
			const int32 s0 = e;
			const int32 s1 = (e + 1) % NumSection;

			const int32 A0 = Verts[f * NumSection + s0];
			const int32 A1 = Verts[f * NumSection + s1];
			const int32 B0 = Verts[(f + 1) * NumSection + s0];
			const int32 B1 = Verts[(f + 1) * NumSection + s1];

			const float U0 = static_cast<float>(SectionU[e] * Options.UVScale);
			const float U1 = static_cast<float>(SectionU[e + 1] * Options.UVScale);
			const int32 Material = Section[s0].EdgeMaterialID;

			// (A0, B0, B1) and (A0, B1, A1) give a face normal equal to the section edge direction
			// rotated +90 degrees in the (Lateral, Vertical) plane. For a section ordered
			// left-to-right across the top and right-to-left along the bottom, that is outward on
			// every edge of the loop, including the two flanks.
			AppendTri(Mesh, A0, B0, B1, Material,
				FVector2f(U0, VNear), FVector2f(U0, VFar), FVector2f(U1, VFar));
			AppendTri(Mesh, A0, B1, A1, Material,
				FVector2f(U0, VNear), FVector2f(U1, VFar), FVector2f(U1, VNear));
		}
	}

	if (!Options.bSectionIsClosed || (!Options.bCapStart && !Options.bCapEnd))
	{
		return;
	}

	// Centroid fan caps. The road sections are shallow and near-convex, so every edge is visible
	// from the centroid and a fan is sufficient.
	double CentroidLateral = 0.0;
	double CentroidVertical = 0.0;
	for (const FSectionVertex& Vertex : Section)
	{
		CentroidLateral += Vertex.Lateral;
		CentroidVertical += Vertex.Vertical;
	}
	CentroidLateral /= NumSection;
	CentroidVertical /= NumSection;

	auto AppendCap = [&](int32 FrameIndex, bool bFacesForward)
	{
		const FFrame& Frame = Frames[FrameIndex];
		const FVector CentroidWorld =
			Frame.Origin + Frame.Right * CentroidLateral + Frame.Up * CentroidVertical;
		const int32 CentroidVert = Mesh.AppendVertex(WorldToLocal.TransformPosition(CentroidWorld));
		const FVector2f CentroidUV(
			static_cast<float>(CentroidLateral * Options.UVScale),
			static_cast<float>(CentroidVertical * Options.UVScale));

		for (int32 s = 0; s < NumSection; ++s)
		{
			const int32 s1 = (s + 1) % NumSection;
			const int32 V0 = Verts[FrameIndex * NumSection + s];
			const int32 V1 = Verts[FrameIndex * NumSection + s1];

			const FVector2f UV0(
				static_cast<float>(Section[s].Lateral * Options.UVScale),
				static_cast<float>(Section[s].Vertical * Options.UVScale));
			const FVector2f UV1(
				static_cast<float>(Section[s1].Lateral * Options.UVScale),
				static_cast<float>(Section[s1].Vertical * Options.UVScale));

			// The section winds clockwise in its own plane, so the plain fan order faces backward
			// along the tangent. The end cap therefore takes the reversed order.
			if (bFacesForward)
			{
				AppendTri(Mesh, CentroidVert, V1, V0, Slots::Flank, CentroidUV, UV1, UV0);
			}
			else
			{
				AppendTri(Mesh, CentroidVert, V0, V1, Slots::Flank, CentroidUV, UV0, UV1);
			}
		}
	};

	if (Options.bCapStart)
	{
		AppendCap(0, false);
	}
	if (Options.bCapEnd)
	{
		AppendCap(NumFrames - 1, true);
	}
}

void AppendPrism(
	FDynamicMesh3& Mesh,
	const TArray<FVector>& BoundaryCCW,
	double BaseZOffset,
	int32 TopMaterialID,
	int32 SideMaterialID,
	const FTransform& WorldToLocal,
	float UVScale)
{
	const int32 Num = BoundaryCCW.Num();
	if (Num < 3)
	{
		return;
	}

	PrepareMesh(Mesh);

	FVector Centroid = FVector::ZeroVector;
	for (const FVector& Point : BoundaryCCW)
	{
		Centroid += Point;
	}
	Centroid /= static_cast<double>(Num);

	const FVector Drop(0.0, 0.0, -FMath::Abs(BaseZOffset));

	TArray<int32> TopVerts;
	TArray<int32> BottomVerts;
	TopVerts.SetNum(Num);
	BottomVerts.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		TopVerts[i] = Mesh.AppendVertex(WorldToLocal.TransformPosition(BoundaryCCW[i]));
		BottomVerts[i] = Mesh.AppendVertex(WorldToLocal.TransformPosition(BoundaryCCW[i] + Drop));
	}

	const int32 TopCentroidVert = Mesh.AppendVertex(WorldToLocal.TransformPosition(Centroid));
	const int32 BottomCentroidVert = Mesh.AppendVertex(WorldToLocal.TransformPosition(Centroid + Drop));

	// Top and bottom faces use planar XY as UVs so the surface texture does not stretch across a
	// junction that is much wider than it is long.
	auto PlanarUV = [UVScale](const FVector& P)
	{
		return FVector2f(static_cast<float>(P.X * UVScale), static_cast<float>(P.Y * UVScale));
	};

	// Ear-clip the cap rather than fanning it from the centroid.
	//
	// A centroid fan is only valid when every edge is visible from the centre. An intersection
	// surface is not: the corner returns arc inward, so on a junction with deep notches a fan emits
	// triangles that spill outside the boundary entirely. Ear clipping handles any simple polygon.
	TArray<FVector2D> Flat;
	Flat.Reserve(Num);
	for (const FVector& Point : BoundaryCCW)
	{
		Flat.Emplace(Point.X, Point.Y);
	}

	TArray<FIndex3i> CapTriangles;
	PolygonTriangulation::TriangulateSimplePolygon(Flat, CapTriangles, false);

	if (CapTriangles.Num() == 0)
	{
		// Degenerate outline: fall back to the fan so the surface is not simply missing.
		for (int32 i = 0; i < Num; ++i)
		{
			CapTriangles.Add(FIndex3i(Num, i, (i + 1) % Num));
		}
		TopVerts.Add(TopCentroidVert);
		BottomVerts.Add(BottomCentroidVert);
	}

	for (const FIndex3i& Tri : CapTriangles)
	{
		if (!TopVerts.IsValidIndex(Tri.A) || !TopVerts.IsValidIndex(Tri.B) || !TopVerts.IsValidIndex(Tri.C))
		{
			continue;
		}

		const FVector2f UVA = PlanarUV(BoundaryCCW.IsValidIndex(Tri.A) ? BoundaryCCW[Tri.A] : Centroid);
		const FVector2f UVB = PlanarUV(BoundaryCCW.IsValidIndex(Tri.B) ? BoundaryCCW[Tri.B] : Centroid);
		const FVector2f UVC = PlanarUV(BoundaryCCW.IsValidIndex(Tri.C) ? BoundaryCCW[Tri.C] : Centroid);

		// Counter-clockwise boundary: this order faces +Z.
		AppendTri(Mesh, TopVerts[Tri.A], TopVerts[Tri.B], TopVerts[Tri.C], TopMaterialID,
			UVA, UVB, UVC);

		// Underside takes the reverse order to face -Z.
		AppendTri(Mesh, BottomVerts[Tri.C], BottomVerts[Tri.B], BottomVerts[Tri.A], SideMaterialID,
			UVC, UVB, UVA);
	}

	// Skirt joining the two faces.
	double Travelled = 0.0;
	const float VTop = 0.0f;
	const float VBottom = static_cast<float>(-FMath::Abs(BaseZOffset) * UVScale);

	for (int32 i = 0; i < Num; ++i)
	{
		const int32 j = (i + 1) % Num;
		const double SegmentLength = FVector::Dist(BoundaryCCW[i], BoundaryCCW[j]);

		const float U0 = static_cast<float>(Travelled * UVScale);
		const float U1 = static_cast<float>((Travelled + SegmentLength) * UVScale);
		Travelled += SegmentLength;

		AppendTri(Mesh, TopVerts[i], BottomVerts[i], TopVerts[j], SideMaterialID,
			FVector2f(U0, VTop), FVector2f(U0, VBottom), FVector2f(U1, VTop));
		AppendTri(Mesh, TopVerts[j], BottomVerts[i], BottomVerts[j], SideMaterialID,
			FVector2f(U1, VTop), FVector2f(U0, VBottom), FVector2f(U1, VBottom));
	}
}

bool SampleGroundHeight(
	const AActor* Context,
	TWeakObjectPtr<ATerraDyneManager>& ManagerCache,
	const FVector2D& WorldXY,
	float& OutHeightZ,
	bool* OutWasFallback)
{
	if (OutWasFallback)
	{
		*OutWasFallback = false;
	}

	if (!Context)
	{
		return false;
	}

	UWorld* World = Context->GetWorld();
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = ManagerCache.Get();
	if (!Manager)
	{
		Manager = Cast<ATerraDyneManager>(
			UGameplayStatics::GetActorOfClass(World, ATerraDyneManager::StaticClass()));
		ManagerCache = Manager;
	}

	if (Manager && Manager->GetNaturalTerrainHeightAtXY(WorldXY, OutHeightZ))
	{
		return true;
	}

	// Either there is no TerraDyne in this world, or no chunk is streamed in at this spot.
	const FVector Start(WorldXY.X, WorldXY.Y, 20000000.0); // 200 km
	const FVector End(WorldXY.X, WorldXY.Y, -20000000.0);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RoadGroundSample), false, Context);
	Params.AddIgnoredActor(Context);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		OutHeightZ = static_cast<float>(Hit.Location.Z);
		if (OutWasFallback)
		{
			*OutWasFallback = true;
		}
		return true;
	}

	return false;
}

double PolylineLength(const TArray<FVector>& Points)
{
	double Total = 0.0;
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		Total += FVector::Dist(Points[i - 1], Points[i]);
	}
	return Total;
}

void ResamplePolyline(const TArray<FVector>& In, double TargetSpacing, TArray<FVector>& Out)
{
	Out.Reset();
	if (In.Num() < 2 || TargetSpacing <= UE_KINDA_SMALL_NUMBER)
	{
		Out = In;
		return;
	}

	const double Total = PolylineLength(In);
	if (Total <= TargetSpacing)
	{
		Out = In;
		return;
	}

	const int32 NumSteps = FMath::Max(1, FMath::RoundToInt(Total / TargetSpacing));
	const double Step = Total / static_cast<double>(NumSteps);

	Out.Reserve(NumSteps + 1);
	Out.Add(In[0]);

	int32 Segment = 0;
	double SegmentStart = 0.0;
	double SegmentLength = FVector::Dist(In[0], In[1]);

	for (int32 i = 1; i < NumSteps; ++i)
	{
		const double Target = i * Step;

		while (Segment + 2 < In.Num() && Target > SegmentStart + SegmentLength)
		{
			SegmentStart += SegmentLength;
			++Segment;
			SegmentLength = FVector::Dist(In[Segment], In[Segment + 1]);
		}

		const double Alpha = (SegmentLength > UE_KINDA_SMALL_NUMBER)
			? FMath::Clamp((Target - SegmentStart) / SegmentLength, 0.0, 1.0)
			: 0.0;
		Out.Add(FMath::Lerp(In[Segment], In[Segment + 1], Alpha));
	}

	Out.Add(In.Last());
}

void SmoothHeights(TArray<FVector>& Points, int32 HalfWindow)
{
	if (HalfWindow <= 0 || Points.Num() < 3)
	{
		return;
	}

	TArray<double> Source;
	Source.Reserve(Points.Num());
	for (const FVector& Point : Points)
	{
		Source.Add(Point.Z);
	}

	const int32 Num = Points.Num();
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 Begin = FMath::Max(0, i - HalfWindow);
		const int32 End = FMath::Min(Num - 1, i + HalfWindow);

		double Sum = 0.0;
		for (int32 k = Begin; k <= End; ++k)
		{
			Sum += Source[k];
		}
		Points[i].Z = Sum / static_cast<double>(End - Begin + 1);
	}
}
}
