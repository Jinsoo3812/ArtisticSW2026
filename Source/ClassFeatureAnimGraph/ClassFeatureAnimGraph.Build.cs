using UnrealBuildTool;

public class ClassFeatureAnimGraph : ModuleRules
{
    public ClassFeatureAnimGraph(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "AnimGraph", "AnimGraphRuntime",
            "BlueprintGraph", "ClassFeature"
        });
    }
}
