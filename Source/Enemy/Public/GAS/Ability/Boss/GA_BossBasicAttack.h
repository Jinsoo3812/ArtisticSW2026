#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/GA_BasicAttack.h"
#include "GA_BossBasicAttack.generated.h"

/** Existing weapon hit-scan attack with the boss-wide mutual-exclusion state. */
UCLASS()
class ENEMY_API UGA_BossBasicAttack : public UGA_BasicAttack
{
	GENERATED_BODY()

public:
	UGA_BossBasicAttack();
	virtual bool ShouldSurviveBehaviorTreeAbort() const override { return true; }

	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	FName GetSelectedAttackId() const { return SelectedAttackId; }
	FName GetPreviousAttackId() const { return PreviousAttackId; }

protected:
	virtual bool PrepareAttack(ABaseEnemy* EnemyOwner) override;
	virtual bool ResolveAttackExecutionData(
		ABaseEnemy* EnemyOwner,
		const FWeaponDefinition& WeaponDefinition,
		FEnemyBasicAttackExecutionData& OutData) const override;
	virtual bool PlayAttackMontage(const FEnemyBasicAttackExecutionData& AttackData) override;
	virtual void OnAttackCommitted() override;

private:
	void OnTimedHitScanStart();
	void OnTimedHitScanEnd();
	void ClearTimedHitScanTimers();

	FName SelectedAttackId;
	FName PreviousAttackId;
	FTimerHandle TimedHitScanStartHandle;
	FTimerHandle TimedHitScanEndHandle;
};
