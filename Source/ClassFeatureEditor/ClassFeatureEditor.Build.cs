using UnrealBuildTool;

public class ClassFeatureEditor : ModuleRules
{
	public ClassFeatureEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"Niagara",
			"BehaviorTreeEditor",
			"AIGraph",
			"AIModule",
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
			"GameplayAbilities",
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
			"Enemy",
			"NPCDialogue",
			"Story",
			"GASCore",
			"WaterAndShip",
			"Water",
			"Landscape",
			"GeometryCore",
			"MeshConversion",
			"MeshDescription",
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
