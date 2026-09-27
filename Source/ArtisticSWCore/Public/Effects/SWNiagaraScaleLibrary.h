#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWNiagaraScaleLibrary.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/** Shared Niagara spawning/tuning entry point. Vendor-specific parameter mapping stays behind this API. */
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

	/** Applies size, authored lifetime, and playback-speed multipliers before activation. */
	UFUNCTION(BlueprintCallable, Category = "Effects|Niagara")
	static void ApplyEffectTuning(
		UNiagaraComponent* Component,
		float SizeScale = 1.0f,
		float LifetimeScale = 1.0f,
		float PlaybackSpeed = 1.0f);

	/** Spawns inactive, applies the scale contract, then activates the effect. */
	UFUNCTION(BlueprintCallable, Category = "Effects|Niagara", meta = (WorldContext = "WorldContextObject"))
	static UNiagaraComponent* SpawnUniformlyScaledSystemAtLocation(
		const UObject* WorldContextObject,
		UNiagaraSystem* System,
		FVector Location,
		FRotator Rotation,
		float UniformScale = 1.0f,
		bool bAutoDestroy = true);

	/** Spawns inactive, applies all effect tuning, then activates. */
	UFUNCTION(BlueprintCallable, Category = "Effects|Niagara", meta = (WorldContext = "WorldContextObject"))
	static UNiagaraComponent* SpawnTunedSystemAtLocation(
		const UObject* WorldContextObject,
		UNiagaraSystem* System,
		FVector Location,
		FRotator Rotation,
		float SizeScale = 1.0f,
		float LifetimeScale = 1.0f,
		float PlaybackSpeed = 1.0f,
		bool bAutoDestroy = true);
};
