// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GA_BasicAttack.generated.h"

class ABaseEnemy;
class ABaseWeapon;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UAnimMontage;
struct FWeaponDefinition;

/** Internal duration GE carrying Cooldown.Enemy.BasicAttack. */
UCLASS(NotBlueprintable)
class ENEMY_API UEnemyBasicAttackCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEnemyBasicAttackCooldownEffect();
};

UCLASS()
class ENEMY_API UGA_BasicAttack : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BasicAttack();

	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	UFUNCTION(BlueprintPure, Category = "Attack|Cooldown")
	float GetAttackCooldownDuration() const { return AttackCooldownDuration; }

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

protected:
	UFUNCTION()
	void OnAttackMontageCompleted();

	UFUNCTION()
	void OnAttackMontageBlendOut();

	UFUNCTION()
	void OnAttackMontageInterrupted();

	UFUNCTION()
	void OnAttackMontageCancelled();

	UFUNCTION()
	void OnHitScanStartEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnHitScanEndEvent(FGameplayEventData Payload);

	void FinishAttack(bool bWasCancelled);
	const FWeaponDefinition* CacheAttackData(ABaseEnemy* EnemyOwner);
	bool PlayAttackMontage(const FWeaponDefinition& WeaponDefinition);
	void StartHitScan();
	void EndHitScan();
	void AddAttackStateTag();
	void RemoveAttackStateTag();

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	TObjectPtr<ABaseWeapon> CachedWeapon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	FGameplayEffectSpecHandle CachedDamageSpecHandle;

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	bool bHitScanActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	bool bAttackFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UAbilityTask_PlayMontageAndWait> AttackMontageTask = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UAbilityTask_WaitGameplayEvent> HitScanStartEventTask = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UAbilityTask_WaitGameplayEvent> HitScanEndEventTask = nullptr;

	/** Single source of truth for this ability's tag-backed GAS cooldown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attack|Cooldown", meta=(ClampMin="0.0", Units="s"))
	float AttackCooldownDuration = 2.0f;

private:
	UPROPERTY()
	FGameplayTagContainer NativeCooldownTags;
};
