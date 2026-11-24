// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AnimGraphInspector : ModuleRules
{
	public AnimGraphInspector(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ToolsetRegistry"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AnimGraph",
			"BlueprintGraph",
			"GraphEditor"
		});
	}
}
