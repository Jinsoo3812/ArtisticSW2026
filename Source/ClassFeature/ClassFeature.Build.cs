using UnrealBuildTool;

public class ClassFeature: ModuleRules
{
    public ClassFeature(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            // 紐⑤뱢 ???대옒?ㅼ쓽 ?뚯뒪 ?뚯씪?먯꽌留??ъ슜??紐⑤뱢 ?섏〈?깆쓣 異붽?
            "Core",
            "CoreUObject",
            "Engine",
            "GASCore"
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            // 紐⑤뱢 ???대옒?ㅼ쓽 ?ㅻ뜑 ?뚯씪?먯꽌遺???ъ슜??紐⑤뱢 ?섏〈?깆쓣 異붽?\
            "GameplayAbilities",
			"GameplayTags",
			"GameplayTasks"
        });

        PublicIncludePaths.AddRange(new string[] {
            // ?덈줈 異붽????뚯씪 寃쎈줈瑜?異붽??섏뿬 include ???대옒???대쫫留??ъ슜?????덇쾶
			"ClassFeature",
			"ClassFeature/Public"
		});
    }
}
