// Copyright (c) 2026 RealtimeTerrain. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "RoadWidthProfile.h"

// Verifies RoadWidthProfile::EvaluateWidth's three distinct regimes (0 / 1 / 2+ keys), each a
// complete answer on its own rather than requiring a caller-side "do I even have enough keys"
// guard the way RoadElevationProfile::EvaluateHeight does -- a single authored key has to visibly
// do something (a smooth local bulge), since it's the very first gesture the gizmo's Scale-mode
// handle produces, not just an intermediate state on the way to a bracketed pair.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadWidthProfileMathTest, "TerraDyne.RoadWidthProfile.WidthMath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoadWidthProfileMathTest::RunTest(const FString& Parameters)
{
	{
		// No authored keys at all: every existing road (WidthKeys defaults empty) must render at
		// exactly its unmodified authored width, at any distance.
		TArray<FRoadWidthKey> Empty;
		TestEqual(TEXT("No keys: unscaled at distance 0"), RoadWidthProfile::EvaluateWidth(Empty, 0.0), 1.0, 0.001);
		TestEqual(TEXT("No keys: unscaled far from the origin too"), RoadWidthProfile::EvaluateWidth(Empty, 50000.0), 1.0, 0.001);
	}
	{
		// One key, no bracketing partner yet -- the falloff radius (500cm) blends smoothly to 1.0.
		TArray<FRoadWidthKey> One;
		One.Add({ 1000.0, 3.0, 0.0 });

		TestEqual(TEXT("One key: exact multiplier right at the key"), RoadWidthProfile::EvaluateWidth(One, 1000.0), 3.0, 0.001);
		TestEqual(TEXT("One key: unscaled exactly at the falloff radius"), RoadWidthProfile::EvaluateWidth(One, 1500.0), 1.0, 0.001);
		TestEqual(TEXT("One key: unscaled well beyond the falloff radius"), RoadWidthProfile::EvaluateWidth(One, 5000.0), 1.0, 0.001);
		TestEqual(TEXT("One key: unscaled the same distance on the other side"), RoadWidthProfile::EvaluateWidth(One, 500.0), 1.0, 0.001);
		// Cubic smoothstep at Alpha=0.5 (halfway through the falloff radius) evaluates to exactly
		// 0.5 (3*0.25 - 2*0.125 = 0.5), landing the blend exactly halfway between 1.0 and the key's
		// own multiplier -- (1.0 + 3.0) / 2 = 2.0.
		TestEqual(TEXT("One key: halfway through the falloff is the exact midpoint blend"), RoadWidthProfile::EvaluateWidth(One, 1250.0), 2.0, 0.01);
	}
	{
		// Two keys, flat (same multiplier, zero slope): constant across the whole authored range,
		// same "flat profile" property RoadElevationProfile.HeightMath already verifies for height.
		TArray<FRoadWidthKey> Flat;
		Flat.Add({ 0.0, 2.0, 0.0 });
		Flat.Add({ 1000.0, 2.0, 0.0 });
		TestEqual(TEXT("Flat profile start"), RoadWidthProfile::EvaluateWidth(Flat, 0.0), 2.0, 0.01);
		TestEqual(TEXT("Flat profile midpoint"), RoadWidthProfile::EvaluateWidth(Flat, 500.0), 2.0, 0.01);
		TestEqual(TEXT("Flat profile end"), RoadWidthProfile::EvaluateWidth(Flat, 1000.0), 2.0, 0.01);
		// Unlike EvaluateHeight, EvaluateWidth clamps outside its own authored range itself --
		// callers don't need their own guard the way ApplyElevationProfile has for HeightKeys.
		TestEqual(TEXT("Flat profile: unscaled just before the first key"), RoadWidthProfile::EvaluateWidth(Flat, -100.0), 1.0, 0.01);
		TestEqual(TEXT("Flat profile: unscaled just past the last key"), RoadWidthProfile::EvaluateWidth(Flat, 1100.0), 1.0, 0.01);
	}
	{
		// Two keys, a genuine widen-then-return S-curve, zero slope at both ends -- not linear.
		TArray<FRoadWidthKey> Bulge;
		Bulge.Add({ 0.0, 1.0, 0.0 });
		Bulge.Add({ 2000.0, 3.0, 0.0 });
		TestEqual(TEXT("Bulge start"), RoadWidthProfile::EvaluateWidth(Bulge, 0.0), 1.0, 0.01);
		TestEqual(TEXT("Bulge end"), RoadWidthProfile::EvaluateWidth(Bulge, 2000.0), 3.0, 0.01);
		TestEqual(TEXT("Bulge midpoint (S-curve symmetry)"), RoadWidthProfile::EvaluateWidth(Bulge, 1000.0), 2.0, 0.01);
		const double QuarterPoint = RoadWidthProfile::EvaluateWidth(Bulge, 500.0);
		TestTrue(TEXT("Bulge quarter-point is not linear (starts flat, so it lags the straight-line value of 1.5)"), QuarterPoint < 1.5);
	}
	return true;
}
