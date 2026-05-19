using UnrealBuildTool;

public class ClassFeatureEditor : ModuleRules
{
	public ClassFeatureEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AnimationBlueprintLibrary",
			"AnimationModifiers",
			"ClassFeature"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"ClassFeatureEditor/Public"
		});
	}
}
