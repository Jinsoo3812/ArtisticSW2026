#include "Effects/SWSplashEffectsNiagaraAdapter.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

bool FSWSplashEffectsNiagaraAdapter::ApplyLifetime(UNiagaraComponent* Component, float LifetimeScale)
{
	UNiagaraSystem* System = Component ? Component->GetAsset() : nullptr;
	if (!System || !System->GetPathName().StartsWith(TEXT("/Game/Resources_Assets/Splash_Effects/"))) return false;
	const FName Name(TEXT("User.Splash_Lifetime"));
	const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetFloatDef(), Name);
	const TOptional<float> DefaultValue = System->GetExposedParameters().GetParameterOptionalValue<float>(Parameter);
	if (!DefaultValue.IsSet()) return false;
	Component->SetVariableFloat(Name, DefaultValue.GetValue() * FMath::Max(0.01f, LifetimeScale));
	return true;
}
