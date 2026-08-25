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
			"LevelEditor",
			"AnimationBlueprintLibrary",
			"AnimationModifiers",
			"ClassFeature",
			"GASCore",
			"WaterAndShip",
			"Water",
			"GeometryCore",
			"MeshConversion",
			"AssetRegistry",
			"ImageCore"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"ClassFeatureEditor/Public"
		});
	}
}
