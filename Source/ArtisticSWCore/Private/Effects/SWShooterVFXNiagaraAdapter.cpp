#include "Effects/SWShooterVFXNiagaraAdapter.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

namespace
{
bool HasFloatParameter(const UNiagaraSystem* System, const FName Name)
{
	if (!System) return false;
	TArray<FNiagaraVariable> Parameters;
	System->GetExposedParameters().GetParameters(Parameters);
	return Parameters.ContainsByPredicate([Name](const FNiagaraVariable& Parameter)
	{
		return Parameter.GetName() == Name
			&& Parameter.GetType() == FNiagaraTypeDefinition::GetFloatDef();
	});
}

bool IsShooterVFXSystem(const UNiagaraSystem* System)
{
	if (!System) return false;
	if (System->GetPathName().StartsWith(TEXT("/Game/Resources_Assets/Shooter_VFXPack/"))) return true;
	return (HasFloatParameter(System, TEXT("User.RibbonWidth")) && HasFloatParameter(System, TEXT("User.RibbonLifeTime")))
		|| HasFloatParameter(System, TEXT("User.Elec_Scale"))
		|| HasFloatParameter(System, TEXT("User.HitScale"));
}

bool ApplyFloatMultiplier(UNiagaraComponent* Component, const FName Name, const float Multiplier)
{
	UNiagaraSystem* System = Component ? Component->GetAsset() : nullptr;
	if (!System) return false;
	const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetFloatDef(), Name);
	const TOptional<float> DefaultValue = System->GetExposedParameters().GetParameterOptionalValue<float>(Parameter);
	if (!DefaultValue.IsSet()) return false;
	Component->SetVariableFloat(Name, DefaultValue.GetValue() * Multiplier);
	return true;
}
}

bool FSWShooterVFXNiagaraAdapter::Apply(
	UNiagaraComponent* Component, float SizeScale, float LifetimeScale, bool& bOutAppliedLifetime)
{
	UNiagaraSystem* System = Component ? Component->GetAsset() : nullptr;
	if (!IsShooterVFXSystem(System))
	{
		bOutAppliedLifetime = false;
		return false;
	}

	const float SafeSizeScale = FMath::Max(0.01f, SizeScale);
	const float SafeLifetimeScale = FMath::Max(0.01f, LifetimeScale);
	bool bAppliedSize = false;
	bOutAppliedLifetime = false;
	// Preserve legacy component-space scaling for continuous projectile trails.
	// Width-only scaling leaves spawn spacing unchanged and produces visible gaps.
	const bool bContinuousProjectile = HasFloatParameter(System, TEXT("User.ProjSpeed"))
		&& HasFloatParameter(System, TEXT("User.SpawnRate"));
	if (!bContinuousProjectile)
	{
		for (const FName Name : {
			FName(TEXT("User.HitScale")), FName(TEXT("User.Scale")), FName(TEXT("User.PartS_ParticleScale")),
			FName(TEXT("User.RibbonWidth")), FName(TEXT("User.SmokeParticleScale")), FName(TEXT("User.Radius")),
			FName(TEXT("User.Elec_Scale")), FName(TEXT("User.Elec_Thickness")),
			FName(TEXT("User.Elec02_Thickness")), FName(TEXT("User.Elec02_DistanceMax")) })
		{
			bAppliedSize |= ApplyFloatMultiplier(Component, Name, SafeSizeScale);
		}
	}
	for (const FName Name : {
		FName(TEXT("User.SmokeLifeTime")), FName(TEXT("User.SmokeRandomLifeTime")),
		FName(TEXT("User.PartS_SmokeLifeTime")), FName(TEXT("User.Elec_LifeTime")),
		FName(TEXT("User.Elec02_Duration")), FName(TEXT("User.RibbonLifeTime")),
		FName(TEXT("User.FireErosionDelay")), FName(TEXT("User.FireErosionDuration")), FName(TEXT("User.Delay")) })
	{
		bOutAppliedLifetime |= ApplyFloatMultiplier(Component, Name, SafeLifetimeScale);
	}
	return bAppliedSize;
}
