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
			"TerraDyne"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
