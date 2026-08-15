using UnrealBuildTool;

public class NPCDialogue : ModuleRules
{
	public NPCDialogue(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"Story",
			"ArtisticSWCore",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayAbilities",
			"InputCore",
			"NetCore",
			"Slate",
			"SlateCore"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"NPCDialogue",
			"NPCDialogue/Public",
			"NPCDialogue/Public/UI"
		});
	}
}
