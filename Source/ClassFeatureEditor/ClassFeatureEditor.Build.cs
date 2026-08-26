using UnrealBuildTool;

public class ClassFeatureEditor : ModuleRules
{
	public ClassFeatureEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"UnrealEd",
			"Kismet",
			"Slate",
			"SlateCore",
			"UMG",
			"UMGEditor",
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
