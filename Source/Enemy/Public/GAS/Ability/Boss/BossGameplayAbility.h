#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GameplayEffect.h"
#include "BossGameplayAbility.generated.h"

class AShipBossEnemy;

UCLASS(NotBlueprintable)
class ENEMY_API UBossAbilityCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBossAbilityCooldownEffect();
};

UCLASS(NotBlueprintable)
class ENEMY_API UBossAbilityStateEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBossAbilityStateEffect();
};

/** Shared server-only cooldown, busy-state, and target helpers for boss abilities. */
UCLASS(Abstract)
class ENEMY_API UBossGameplayAbility : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UBossGameplayAbility();
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	float GetBossCooldownDuration() const { return CooldownDuration; }
	FGameplayTag GetBossCooldownTag() const { return CooldownTag; }
	FGameplayTag GetStartupGameplayCueTag() const { return StartupGameplayCueTag; }
	FGameplayTag GetImpactGameplayCueTag() const { return ImpactGameplayCueTag; }

	/** A committed mobility ability can retain the destination selected by the BT. */
	virtual bool OwnsPreselectedDestinationAfterCommit() const { return false; }

protected:
	void SetBossAbilityTags(FGameplayTag AbilityTag, FGameplayTag InCooldownTag);
	void ExecuteStartupGameplayCue() const;
	AShipBossEnemy* GetBossAvatar() const;
	AActor* GetBossTarget() const;
	bool PrepareStrengthAttack(float AttackCoefficient);
	FGameplayEffectSpecHandle CommittedDamageSpec;
	bool ApplyDamageToTarget(
		AActor* Target,
		const FHitResult* HitResult = nullptr) const;
	FActiveGameplayEffectHandle ApplyTimedStateTag(
		UAbilitySystemComponent& TargetASC,
		FGameplayTag StateTag,
		float Duration) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Cooldown", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownDuration = 5.0f;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Boss|Cooldown")
	FGameplayTag CooldownTag;

	/** One-shot cosmetic feedback emitted by the authoritative GA after commit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Feedback", meta = (Categories = "GameplayCue"))
	FGameplayTag StartupGameplayCueTag;

	/** Added to outgoing damage specs and executed by the victim after confirmed health loss. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Feedback", meta = (Categories = "GameplayCue.Impact"))
	FGameplayTag ImpactGameplayCueTag;

private:
	UPROPERTY()
	FGameplayTagContainer NativeCooldownTags;
};
