using UnrealBuildTool;

public class ShipUpgradeUI : ModuleRules
{
	public ShipUpgradeUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"WaterAndShip"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ClassFeature",
			"InputCore",
			"Slate",
			"SlateCore"
		});
	}
}
