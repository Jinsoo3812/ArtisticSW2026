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
			"ArtisticSWCore",
			"ClassFeature",
			"Enemy",
			"NPCDialogue",
			"Story",
			"GASCore",
			"WaterAndShip",
			"Water",
			"Landscape",
			"GeometryCore",
			"MeshConversion",
			"AssetRegistry",
			"PropertyEditor",
			"ImageCore",
			"MaterialEditor",
			"RHI"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"ClassFeatureEditor/Public"
		});
	}
}
