#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHitClassicNiagaraScaleAuthoringTest,
	"ArtisticSW.WaterAndShip.Niagara.HitClassicScaleAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitClassicNiagaraScaleAuthoringTest::RunTest(const FString& Parameters)
{
	const TCHAR* AssetPath = TEXT("/Game/Resources_Assets/Shooter_VFXPack/Particles/HitsAndExplosions/P_Hit_Classic_02.P_Hit_Classic_02");
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, AssetPath);
	if (!TestNotNull(TEXT("P_Hit_Classic_02 loads as a Niagara system"), System))
	{
		return false;
	}

	TArray<FNiagaraVariable> ExposedParameters;
	System->GetExposedParameters().GetParameters(ExposedParameters);
	FString ParameterList;
	for (const FNiagaraVariable& Parameter : ExposedParameters)
	{
		ParameterList += ParameterList.IsEmpty() ? TEXT("") : TEXT(", ");
		ParameterList += FString::Printf(
			TEXT("%s:%s"),
			*Parameter.GetName().ToString(),
			*Parameter.GetType().GetName());
	}
	AddInfo(FString::Printf(
		TEXT("System=%s Emitters=%d ExposedParameters=[%s] FixedBounds=%s"),
		*System->GetPathName(),
		System->GetEmitterHandles().Num(),
		*ParameterList,
		*System->GetFixedBounds().ToString()));

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		FString RendererList;
		if (Data)
		{
			for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				RendererList += RendererList.IsEmpty() ? TEXT("") : TEXT(", ");
				RendererList += GetNameSafe(Renderer ? Renderer->GetClass() : nullptr);
			}
		}
		AddInfo(FString::Printf(
			TEXT("Emitter=%s Enabled=%s LocalSpace=%s SimTarget=%d Renderers=[%s]"),
			*Handle.GetName().ToString(),
			Handle.GetIsEnabled() ? TEXT("true") : TEXT("false"),
			Data && Data->bLocalSpace ? TEXT("true") : TEXT("false"),
			Data ? static_cast<int32>(Data->SimTarget) : -1,
			*RendererList));
	}
	return true;
}

#endif
