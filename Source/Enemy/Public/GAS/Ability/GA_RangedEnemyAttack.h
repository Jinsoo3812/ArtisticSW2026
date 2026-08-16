#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_RangedEnemyAttack.generated.h"

class ARangedEnemy;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;

/** Server-only projectile attack used by ARangedEnemy. */
UCLASS()
class ENEMY_API UGA_RangedEnemyAttack : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_RangedEnemyAttack();

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
	void OnFireProjectileEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnAttackMontageCompleted();

	UFUNCTION()
	void OnAttackMontageBlendOut();

	UFUNCTION()
	void OnAttackMontageInterrupted();

	UFUNCTION()
	void OnAttackMontageCancelled();

	bool FireProjectile();
	bool PlayAttackMontage();
	void FinishAttack(bool bWasCancelled);
	void AddAttackStateTag();
	void RemoveAttackStateTag();

	UPROPERTY(Transient)
	TObjectPtr<ARangedEnemy> CachedEnemy = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CachedTarget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_PlayMontageAndWait> AttackMontageTask = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitGameplayEvent> FireProjectileEventTask = nullptr;

	bool bProjectileFired = false;
	bool bFinishingAttack = false;
};
