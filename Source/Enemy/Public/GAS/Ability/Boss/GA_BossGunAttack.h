#pragma once
#include "CoreMinimal.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossAI/BossGunAimLine.h"
#include "GA_BossGunAttack.generated.h"

class UBossGunCombatComponent;
class UAbilityTask_PlayMontageAndWait;

/** Server timing/state machine only; component queries sockets/traces and actor displays cues. */
UCLASS(HideCategories = ("Boss|Cooldown"))
class ENEMY_API UGA_BossGunAttack : public UBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_BossGunAttack();
	virtual bool ShouldSurviveBehaviorTreeAbort() const override { return true; }
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
protected:
	virtual bool IsBurst() const { return false; }
private:
	void BeginPreparation();
	void BeginLock();
	void UpdateAim();
	void FireShot();
	void BeginRecovery();
	void CompleteAttack();
	void Finish(bool bCancelled);
	void JumpMontage(FName Section);
	void Schedule(void (UGA_BossGunAttack::*Callback)(), double Deadline);
	UPROPERTY(Transient) TObjectPtr<UBossGunCombatComponent> Combat;
	UPROPERTY(Transient) TObjectPtr<AActor> AttackTarget;
	UPROPERTY(Transient) TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;
	UPROPERTY(Transient) FGunnerBossShotPattern Pattern;
	FTimerHandle PhaseTimer;
	FTimerHandle AimTimer;
	uint32 AttackId = 0;
	int32 ShotCount = 1;
	int32 ShotsFired = 0;
	bool bFiredCurrentStep = false;
	bool bCommitted = false;
	EBossGunAimPhase Phase = EBossGunAimPhase::Hidden;
	double PreparationEnd = 0.;
	float PhaseStart = 0.f;
	float PhaseEnd = 0.f;
};

/** Same implementation, different settings/tag/cooldown; only the burst count is variable. */
UCLASS()
class ENEMY_API UGA_BossGunBurst : public UGA_BossGunAttack
{
	GENERATED_BODY()
public:
	UGA_BossGunBurst();
protected:
	virtual bool IsBurst() const override { return true; }
};
