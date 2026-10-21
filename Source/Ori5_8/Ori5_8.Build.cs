// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Ori5_8 : ModuleRules
{
	public Ori5_8(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Ori5_8",
			"Ori5_8/Variant_Platforming",
			"Ori5_8/Variant_Platforming/Animation",
			"Ori5_8/Variant_Combat",
			"Ori5_8/Variant_Combat/AI",
			"Ori5_8/Variant_Combat/Animation",
			"Ori5_8/Variant_Combat/Gameplay",
			"Ori5_8/Variant_Combat/Interfaces",
			"Ori5_8/Variant_Combat/UI",
			"Ori5_8/Variant_SideScrolling",
			"Ori5_8/Variant_SideScrolling/AI",
			"Ori5_8/Variant_SideScrolling/Gameplay",
			"Ori5_8/Variant_SideScrolling/Interfaces",
			"Ori5_8/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
