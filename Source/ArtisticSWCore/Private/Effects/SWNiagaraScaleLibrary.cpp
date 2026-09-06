#include "Effects/SWNiagaraScaleLibrary.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const FName USWNiagaraScaleLibrary::UniformScaleParameterName(TEXT("User.EffectScale"));

bool USWNiagaraScaleLibrary::SupportsUniformEffectScale(const UNiagaraSystem* System)
{
	if (!System)
	{
		return false;
	}

	TArray<FNiagaraVariable> Parameters;
	System->GetExposedParameters().GetParameters(Parameters);
	return Parameters.ContainsByPredicate(
		[](const FNiagaraVariable& Parameter)
		{
			return Parameter.GetName() == UniformScaleParameterName
				&& Parameter.GetType() == FNiagaraTypeDefinition::GetFloatDef();
		});
}

bool USWNiagaraScaleLibrary::ApplyUniformEffectScale(
	UNiagaraComponent* Component,
	float UniformScale)
{
	if (!Component)
	{
		return false;
	}

	const float SafeScale = FMath::Max(0.01f, UniformScale);
	const bool bUsesScaleContract = SupportsUniformEffectScale(Component->GetAsset());
	Component->SetRelativeScale3D(bUsesScaleContract ? FVector::OneVector : FVector(SafeScale));
	if (bUsesScaleContract)
	{
		Component->SetVariableFloat(UniformScaleParameterName, SafeScale);
	}
	return bUsesScaleContract;
}

UNiagaraComponent* USWNiagaraScaleLibrary::SpawnUniformlyScaledSystemAtLocation(
	const UObject* WorldContextObject,
	UNiagaraSystem* System,
	FVector Location,
	FRotator Rotation,
	float UniformScale,
	bool bAutoDestroy)
{
	if (!WorldContextObject || !System)
	{
		return nullptr;
	}

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		WorldContextObject,
		System,
		Location,
		Rotation,
		FVector::OneVector,
		bAutoDestroy,
		false);
	if (Component)
	{
		ApplyUniformEffectScale(Component, UniformScale);
		Component->Activate(true);
	}
	return Component;
}
