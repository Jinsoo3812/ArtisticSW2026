using UnrealBuildTool;

public class Enemy: ModuleRules
{
    public Enemy(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            // 소스파일에서 사용할 모듈을 작성하는 곳
            "Core",
            "CoreUObject",
            "Engine",
            "UMG"
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            // 헤더파일에서부터 사용할 모듈을 작성하는 곳
            "GameplayAbilities",
            "GameplayTasks",
            "GameplayTags",
            "InputCore",
            "AIModule",
            "NavigationSystem",
            "ArtisticSWCore",
            "ClassFeature",
            "Story",
            "GASCore",
            "WaterAndShip",
            "Water",
            "AnimGraphRuntime",
        });

        PublicIncludePaths.AddRange(new string[] {
            // Enemy 내 파일 접근을 용이하게 하기 위해서 include 경로 추가
			"Enemy",
			"Enemy/Public",
            "Enemy/Public/AI",
            "Enemy/Public/Animation",
            "Enemy/Public/GAS",
            "Enemy/Public/ShipAI",
            "Enemy/Public/UI"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd"
            });
        }
    }
}
