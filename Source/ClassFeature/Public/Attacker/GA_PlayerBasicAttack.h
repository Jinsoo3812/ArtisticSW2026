#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GameplayEffectTypes.h"
#include "GA_PlayerBasicAttack.generated.h"

class ASwordItem;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UAnimMontage;

/** Player basic melee attack driven by montage gameplay-event notifies. */
UCLASS()
class CLASSFEATURE_API UGA_PlayerBasicAttack : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_PlayerBasicAttack();

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

private:
	bool CacheAttackData();
	bool PlayAttackMontage();
	void StartHitScan();
	void EndHitScan();
	void FinishAttack(bool bWasCancelled);
	void AddAttackStateTag();
	void RemoveAttackStateTag();

	UPROPERTY()
	TObjectPtr<ASwordItem> CachedSword;

	UPROPERTY()
	TObjectPtr<UAnimMontage> CachedAttackMontage;

	FGameplayEffectSpecHandle CachedDamageSpecHandle;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> AttackMontageTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> HitScanStartEventTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> HitScanEndEventTask;

	float CachedAttackMontagePlayRate = 1.0f;
	bool bHitScanActive = false;
	bool bAttackFinished = false;
};
