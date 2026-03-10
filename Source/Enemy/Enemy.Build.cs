using UnrealBuildTool;

public class Enemy: ModuleRules
{
    public Enemy(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            // 모듈 내 클래스의 소스 파일에서만 사용할 모듈 의존성을 추가
            "Core",
            "CoreUObject",
            "Engine"
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            // 모듈 내 클래스의 헤더 파일에서부터 사용될 모듈 의존성을 추가
        });

        PublicIncludePaths.AddRange(new string[] {
            // 새로 추가된 파일 경로를 추가하여 include 시 클래스 이름만 사용할 수 있게
			"Enemy",
			"Enemy/Public"
		});
    }
}
