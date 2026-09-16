#include "Effects/SWSplashEffectsNiagaraAdapter.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

namespace
{
	bool MultiplyFloatParameter(
		UNiagaraComponent* Component,
		UNiagaraSystem* System,
		const FName Name,
		const float Multiplier)
	{
		const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetFloatDef(), Name);
		const TOptional<float> DefaultValue =
			System->GetExposedParameters().GetParameterOptionalValue<float>(Parameter);
		if (!DefaultValue.IsSet())
		{
			return false;
		}

		Component->SetVariableFloat(Name, DefaultValue.GetValue() * FMath::Max(0.01f, Multiplier));
		return true;
	}
}

bool FSWSplashEffectsNiagaraAdapter::Apply(
	UNiagaraComponent* Component,
	float SizeScale,
	float LifetimeScale,
	bool& bOutUsesAuthoredLifetime)
{
	UNiagaraSystem* System = Component ? Component->GetAsset() : nullptr;
	if (!System || !System->GetPathName().StartsWith(TEXT("/Game/Resources_Assets/Splash_Effects/"))) return false;

	// Stream Splash uses a different contract from the pack's impact splashes.
	// Particle size is independent of spread/velocity: do not change the stream
	// motion or gameplay pull radius when tuning its visual particle size.
	if (System->GetPathName() == TEXT("/Game/Resources_Assets/Splash_Effects/Effects/NS_Stream_Splash_01.NS_Stream_Splash_01"))
	{
		const bool bUsesAuthoredSize = MultiplyFloatParameter(
			Component, System, FName(TEXT("User.Particles_Scale")), SizeScale);
		bOutUsesAuthoredLifetime |= MultiplyFloatParameter(
			Component, System, FName(TEXT("User.Lifetime")), LifetimeScale);
		return bUsesAuthoredSize;
	}

	// This pack authors User.Scale as its master spatial control. It feeds particle
	// size, velocity and placement inside the system, so component transform scale
	// is not an equivalent substitute.
	const bool bUsesAuthoredSize = MultiplyFloatParameter(
		Component, System, FName(TEXT("User.Scale")), SizeScale);
	bOutUsesAuthoredLifetime |= MultiplyFloatParameter(
		Component, System, FName(TEXT("User.Splash_Lifetime")), LifetimeScale);
	return bUsesAuthoredSize;
}
