using UnrealBuildTool;

public class GASCore: ModuleRules
{
    public GASCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            // GASCore의 .cpp 파일에서만 필요한 모듈 의존성입니다.
            "Core",
            "CoreUObject",
            "Engine",
            "ArtisticSWCore"
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            // GASCore의 public 헤더가 노출하는 GAS 관련 모듈입니다.
            "GameplayAbilities",
            "GameplayTasks",
            "GameplayTags"
        });

        PublicIncludePaths.AddRange(new string[] {
            // 모듈 내부/외부에서 GASCore public 헤더를 짧은 경로로 include할 수 있게 합니다.
			"GASCore",
			"GASCore/Public"
		});
    }
}
