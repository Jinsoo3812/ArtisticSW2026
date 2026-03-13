// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ArtisticSW2026 : ModuleRules
{
	public ArtisticSW2026(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate",
            "ArtisticSWCore"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ArtisticSW2026",
			"ArtisticSW2026/Variant_Platforming",
			"ArtisticSW2026/Variant_Platforming/Animation",
			"ArtisticSW2026/Variant_Combat",
			"ArtisticSW2026/Variant_Combat/AI",
			"ArtisticSW2026/Variant_Combat/Animation",
			"ArtisticSW2026/Variant_Combat/Gameplay",
			"ArtisticSW2026/Variant_Combat/Interfaces",
			"ArtisticSW2026/Variant_Combat/UI",
			"ArtisticSW2026/Variant_SideScrolling",
			"ArtisticSW2026/Variant_SideScrolling/AI",
			"ArtisticSW2026/Variant_SideScrolling/Gameplay",
			"ArtisticSW2026/Variant_SideScrolling/Interfaces",
			"ArtisticSW2026/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
