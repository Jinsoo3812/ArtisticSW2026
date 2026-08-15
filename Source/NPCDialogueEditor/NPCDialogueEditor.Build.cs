using UnrealBuildTool;

public class NPCDialogueEditor : ModuleRules
{
	public NPCDialogueEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"Core",
			"CoreUObject",
			"Engine",
			"Kismet",
			"NPCDialogue",
			"Slate",
			"SlateCore",
			"UMG",
			"UMGEditor",
			"UnrealEd"
		});
	}
}
