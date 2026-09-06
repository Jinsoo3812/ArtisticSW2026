#include "Effects/SWNiagaraScaleLibrary.h"
#include "Effects/SWShooterVFXNiagaraAdapter.h"
#include "Effects/SWSplashEffectsNiagaraAdapter.h"

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

void USWNiagaraScaleLibrary::ApplyEffectTuning(
	UNiagaraComponent* Component,
	float SizeScale,
	float LifetimeScale,
	float PlaybackSpeed)
{
	if (!Component)
	{
		return;
	}

	const float SafeSizeScale = FMath::Max(0.01f, SizeScale);
	const float SafeLifetimeScale = FMath::Max(0.01f, LifetimeScale);
	bool bUsesAuthoredLifetime = false;
	const bool bUsesShooterAdapter = FSWShooterVFXNiagaraAdapter::Apply(
		Component, SafeSizeScale, SafeLifetimeScale, bUsesAuthoredLifetime);
	bUsesAuthoredLifetime |= FSWSplashEffectsNiagaraAdapter::ApplyLifetime(Component, SafeLifetimeScale);
	if (bUsesShooterAdapter)
	{
		Component->SetRelativeScale3D(FVector::OneVector);
	}
	else
	{
		ApplyUniformEffectScale(Component, SafeSizeScale);
	}
	// Assets without exposed lifetime controls can only extend their whole simulation.
	// Keep that limitation in the generic layer instead of silently ignoring the panel value.
	const float EffectivePlaybackSpeed = bUsesAuthoredLifetime
		? FMath::Max(0.01f, PlaybackSpeed)
		: FMath::Max(0.01f, PlaybackSpeed) / SafeLifetimeScale;
	Component->SetCustomTimeDilation(EffectivePlaybackSpeed);
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

UNiagaraComponent* USWNiagaraScaleLibrary::SpawnTunedSystemAtLocation(
	const UObject* WorldContextObject,
	UNiagaraSystem* System,
	FVector Location,
	FRotator Rotation,
	float SizeScale,
	float LifetimeScale,
	float PlaybackSpeed,
	bool bAutoDestroy)
{
	if (!WorldContextObject || !System)
	{
		return nullptr;
	}

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		WorldContextObject, System, Location, Rotation, FVector::OneVector,
		bAutoDestroy, false);
	if (Component)
	{
		ApplyEffectTuning(Component, SizeScale, LifetimeScale, PlaybackSpeed);
		Component->Activate(true);
	}
	return Component;
}
