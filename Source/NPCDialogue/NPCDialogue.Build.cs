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
			"NetCore"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"NPCDialogue",
			"NPCDialogue/Public"
		});
	}
}
