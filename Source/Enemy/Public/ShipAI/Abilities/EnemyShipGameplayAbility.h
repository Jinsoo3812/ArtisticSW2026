#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "EnemyShipGameplayAbility.generated.h"

/** Internal duration-only GE used by native Enemy Ship ability cooldowns. */
UCLASS(NotBlueprintable)
class ENEMY_API UEnemyShipAbilityCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEnemyShipAbilityCooldownEffect();
};

/** Server-authoritative base with a per-ability, tag-backed GAS cooldown. */
UCLASS(Abstract, Blueprintable)
class ENEMY_API UEnemyShipGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyShipGameplayAbility();

	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Ability|Cooldown")
	float GetCooldownDuration() const { return CooldownDurationSeconds; }

protected:
	void SetNativeAbilityTag(FGameplayTag AbilityTag);
	void SetNativeAbilityAndCooldownTags(FGameplayTag AbilityTag, FGameplayTag InCooldownTag);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Ability|Cooldown", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownDurationSeconds = 5.0f;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Ability|Cooldown")
	FGameplayTag CooldownTag;

private:
	UPROPERTY()
	FGameplayTagContainer NativeCooldownTags;
};
