using UnrealBuildTool;

public class ClassFeature: ModuleRules
{
    public ClassFeature(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            // 모듈 내 클래스의 소스 파일에서만 사용할 모듈 의존성을 추가
            "Core",
            "CoreUObject",
            "Engine",
            "GASCore",
            "InputCore",
            "EnhancedInput",
            "ArtisticSWCore",
            "UMG"
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            // 모듈 내 클래스의 헤더 파일에서부터 사용될 모듈 의존성을 추가
            "GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
            "EnhancedInput",    // <--- InputActionValue.h 등을 위해 필요
            "GASCore",          // <--- BaseCharacter, GASInputID.h를 위해 필요
            "ArtisticSWCore",   // <--- BaseItem.h를 위해 필요
            "InputCore"
        });

        PublicIncludePaths.AddRange(new string[] {
            // 새로 추가된 파일 경로를 추가하여 include 시 클래스 이름만 사용할 수 있게
			"ClassFeature",
			"ClassFeature/Public",
            "ClassFeature/Public/Crafter",
            "ClassFeature/Public/Attacker",
            "ClassFeature/Public/Inventory",
            "ClassFeature/Public/UI"
        });
    }
}
