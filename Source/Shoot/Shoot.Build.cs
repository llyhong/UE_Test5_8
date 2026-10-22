// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Shoot : ModuleRules
{
	public Shoot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"GeometryCollectionEngine",
			"Niagara",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Shoot",
			"Shoot/Variant_Platforming",
			"Shoot/Variant_Platforming/Animation",
			"Shoot/Variant_Combat",
			"Shoot/Variant_Combat/AI",
			"Shoot/Variant_Combat/Animation",
			"Shoot/Variant_Combat/Gameplay",
			"Shoot/Variant_Combat/Interfaces",
			"Shoot/Variant_Combat/UI",
			"Shoot/Variant_SideScrolling",
			"Shoot/Variant_SideScrolling/AI",
			"Shoot/Variant_SideScrolling/Gameplay",
			"Shoot/Variant_SideScrolling/Interfaces",
			"Shoot/Variant_SideScrolling/UI",
			"Shoot/Public/NPC"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
