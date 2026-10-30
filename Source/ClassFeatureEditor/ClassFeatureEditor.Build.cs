using UnrealBuildTool;

public class ClassFeatureEditor : ModuleRules
{
	public ClassFeatureEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"AudioEditor",
			"RenderCore",
			"RHI",
			"PhysicsCore",
			"Json",
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
			"AnimGraph",
			"AnimGraphRuntime",
			"ArtisticSWCore",
			"ClassFeature",
			"GASCore",
			"WaterAndShip",
			"Water",
			"Landscape",
			"GeometryCore",
			"MeshConversion",
			"MeshDescription",
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
