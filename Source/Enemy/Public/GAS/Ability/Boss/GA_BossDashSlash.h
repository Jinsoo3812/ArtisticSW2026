#pragma once

#include "CoreMinimal.h"
#include "BossAI/BossDeckPointSelector.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GA_BossDashSlash.generated.h"

class UAbilityTask_WaitDelay;
class UAnimMontage;

UCLASS()
class ENEMY_API UGA_BossDashSlash : public UBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BossDashSlash();

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
	void BeginDash();

	void TickDash();
	void ApplyDashHit(const FVector& SegmentStart, const FVector& SegmentEnd);
	bool ResolveDestination();
	void FinishDash(bool bWasCancelled);
	void ClearDashState();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash")
	TObjectPtr<UAnimMontage> DashMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.0", Units = "s"))
	float WindupDuration = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.05", Units = "s"))
	float DashDuration = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float DashHitRadius = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.0"))
	float Damage = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Point")
	FBossDestinationSelectionSettings PointSelectionSettings;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> WindupTask = nullptr;

	FActiveGameplayEffectHandle DashStateHandle;
	FTimerHandle DashTimerHandle;
	FVector DashStartLocal = FVector::ZeroVector;
	FVector DashEndLocal = FVector::ZeroVector;
	FVector PreviousWorldLocation = FVector::ZeroVector;
	float DashElapsed = 0.0f;
	float DashTickInterval = 1.0f / 60.0f;
	bool bTargetHit = false;
};
