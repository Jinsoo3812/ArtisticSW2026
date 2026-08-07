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
	void OnHitScanTickEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnHitScanEndEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnComboCommitEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnComboInputEvent(FGameplayEventData Payload);

private:
	bool CacheAttackData();
	bool CacheComboSections(const TArray<FName>& ConfiguredSections);
	bool PlayAttackMontage();
	void CommitBufferedCombo();
	void HoldSectionForCommit(FName SectionName);
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
	TObjectPtr<UAbilityTask_WaitGameplayEvent> HitScanTickEventTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> HitScanEndEventTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> ComboCommitEventTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> ComboInputEventTask;

	TArray<FName> CachedComboSections;
	float CachedAttackMontagePlayRate = 1.0f;
	int32 CurrentComboIndex = INDEX_NONE;
	bool bComboInputBuffered = false;
	bool bHitScanActive = false;
	bool bAttackFinished = false;
	bool bServerCombatPoseRefreshAcquired = false;
};
