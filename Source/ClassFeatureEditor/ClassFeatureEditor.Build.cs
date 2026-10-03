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
			"EngineSettings",
			"DeveloperToolSettings",
			"EnhancedInput",
			"InputCore",
			"GameplayTags",
			"UnrealEd",
			"Kismet",
			"BlueprintGraph",
			"KismetCompiler",
			"Slate",
			"SlateCore",
			"UMG",
			"UMGEditor",
			"LevelEditor",
			"AnimationBlueprintLibrary",
			"AnimationModifiers",
			"ArtisticSWCore",
			"ClassFeature",
			"GASCore",
			"WaterAndShip",
			"Water",
			"GeometryCore",
			"MeshConversion",
			"AssetRegistry",
			"PropertyEditor",
			"ImageCore"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"ClassFeatureEditor/Public"
		});
	}
}
