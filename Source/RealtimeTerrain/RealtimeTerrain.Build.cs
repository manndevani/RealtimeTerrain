// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RealtimeTerrain : ModuleRules
{
	public RealtimeTerrain(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"EnhancedInput",
			"GeometryCore",
			"GeometryFramework",
			"GeometryScriptingCore",
			"DynamicMesh",
			"TerraDyne"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			// RoadXodrExport.cpp builds its OpenDRIVE document as an in-memory FXmlNode tree
			// (mirroring RoadBuilder's own ExportXodr approach) and serialises it via
			// FXmlFile::WriteNodeHierarchy -- no disk I/O needed, so the exporter stays a pure,
			// testable function.
			"XmlParser"
		});

		if (Target.bBuildEditor)
		{
			// FAutomationEditorCommonUtils::CreateNewMap(), used by the editor-only automation tests
			// under Private/Tests/ (guarded by WITH_EDITOR) to spin up a throwaway world to spawn
			// actors into.
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
