#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_WaterBombCannonMode.generated.h"

class ACannon;
class AWaterBombCannonball;

/**
 * Persistent server-side ability that changes the cannon currently ridden by the
 * avatar into Water Bomb mode. Pressing the cannon toggle again cancels this GA.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API UGA_WaterBombCannonMode : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_WaterBombCannonMode();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** Projectile spawned by the ridden cannon while this ability is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Projectile")
	TSubclassOf<AWaterBombCannonball> ProjectileClass;

	/** Duration shared by the enemy-ship cannon disable and onboard-enemy slow effects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effect", meta = (ClampMin = "0.1", Units = "s"))
	float EffectDurationSeconds = 5.0f;

	/** 1.0 is normal speed; 0.5 makes both attack montage and attack cadence 50% speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effect", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AttackSpeedMultiplier = 0.5f;

private:
	ACannon* FindRiddenCannon() const;

	TWeakObjectPtr<ACannon> ActiveCannon;
	bool bAddedActivationTag = false;
};
