#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWNiagaraScaleLibrary.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/** Shared runtime contract for Niagara systems authored with User.EffectScale. */
UCLASS()
class ARTISTICSWCORE_API USWNiagaraScaleLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const FName UniformScaleParameterName;

	/** True when the system exposes the float User.EffectScale authoring contract. */
	UFUNCTION(BlueprintPure, Category = "Effects|Niagara")
	static bool SupportsUniformEffectScale(const UNiagaraSystem* System);

	/**
	 * Applies a uniform scale without double-scaling compliant effects.
	 * Authored systems receive User.EffectScale and stay at unit component scale;
	 * legacy systems fall back to component transform scale.
	 */
	UFUNCTION(BlueprintCallable, Category = "Effects|Niagara")
	static bool ApplyUniformEffectScale(UNiagaraComponent* Component, float UniformScale);

	/** Spawns inactive, applies the scale contract, then activates the effect. */
	UFUNCTION(BlueprintCallable, Category = "Effects|Niagara", meta = (WorldContext = "WorldContextObject"))
	static UNiagaraComponent* SpawnUniformlyScaledSystemAtLocation(
		const UObject* WorldContextObject,
		UNiagaraSystem* System,
		FVector Location,
		FRotator Rotation,
		float UniformScale = 1.0f,
		bool bAutoDestroy = true);
};
