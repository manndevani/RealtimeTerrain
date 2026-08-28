// Copyright (c) 2026 GregOrigin. All Rights Reserved.
using UnrealBuildTool;

public class TerraDyne : ModuleRules
{
	public TerraDyne(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;


		// Core dependencies
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Landscape",
			"RenderCore",
			"RHI",
			"GeometryCore",
			"UMG",
			"VirtualHeightfieldMesh"
		});

		// Plugin dependencies (Must be enabled in .uproject)
		PublicDependencyModuleNames.AddRange(new string[] {
			"GeometryFramework",
			"CesiumRuntime"
		});

		// Private dependencies
		PrivateDependencyModuleNames.AddRange(new string[] {
			"GeometryScriptingCore",
			"Slate",
            "SlateCore",
            "DeveloperSettings",
            "ApplicationCore",
            "NavigationSystem",
            "MeshDescription",
            "StaticMeshDescription",
            "CelestialVault",
            "DaySequence",
            "CesiumRuntime",
            "GDAL",
            "Projects",
            "Json",
            "JsonUtilities",
            "ImageWrapper",
            "XmlParser"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("Foliage");
			// Native OS Save/Open file dialogs for the Levels panel (editor/PIE only — not
			// meaningfully available in packaged non-editor builds).
			PrivateDependencyModuleNames.Add("DesktopPlatform");
		}
	}
}
