#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckWalkTypes.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GAS/SWGameplayEffectContext.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/Tasks/BossSlashDashTypes.h"
#include "GA_BossDashSlash.generated.h"

class UAbilityTask_BossSlashDashExecution;
class UAnimMontage;
class UPrimitiveComponent;
class UPathCombatPresentationDataAsset;
class UStaticMeshComponent;

/** Native fallback. A presentation Data Asset may replace this class. */
UCLASS(NotBlueprintable)
class ENEMY_API UBossDashSlashTelegraphEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBossDashSlashTelegraphEffect();
};

/** Native 1.5 second residual path fallback. A presentation Data Asset may replace it. */
UCLASS(NotBlueprintable)
class ENEMY_API UBossDashSlashExecutionPathEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBossDashSlashExecutionPathEffect();
};

UCLASS()
class ENEMY_API UGA_BossDashSlash : public UBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BossDashSlash();
	virtual void PostLoad() override;
	virtual bool ShouldSurviveBehaviorTreeAbort() const override { return true; }
	virtual bool OwnsPreselectedDestinationAfterCommit() const override { return true; }

	FName GetWindupSectionName() const { return MontageConfig.WindupEnterSectionName; }
	FName GetWindupHoldSectionName() const { return MontageConfig.WindupHoldSectionName; }
	FName GetDashSlashSectionName() const { return MontageConfig.AttackSectionName; }
	FName GetDashHoldSectionName() const { return MontageConfig.TravelHoldSectionName; }
	FName GetRecoverySectionName() const { return MontageConfig.RecoverySectionName; }
	float GetWindupHoldDuration() const { return MontageConfig.WindupHoldDuration; }
	float GetMinimumDashDistance() const { return MinimumDashDistance; }
	UPathCombatPresentationDataAsset* GetPathPresentation() const { return PathPresentation; }
	const FDashSlashMontageConfig& GetMontageConfig() const { return MontageConfig; }

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
	bool StartDashExecution(float TotalWaitOverride = -1.f, bool bSkipWindup = false, bool bSkipRecovery = false);
	UFUNCTION() virtual void HandleExecutionCompleted();
	UFUNCTION() virtual void HandleExecutionFailed();
	UFUNCTION() void HandleExecutionHit(AActor* Target, const FHitResult& Hit);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Presentation", meta = (Categories = "GameplayCue"))
	FGameplayTag ChargingGameplayCueTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage")
	FDashSlashMontageConfig MontageConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.05", Units = "s"))
	float DashDuration = 0.45f;

	/** Authoritative safety check in addition to the destination selector filter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float MinimumDashDistance = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float DashHitRadius = 120.0f;

	UPROPERTY(EditDefaultsOnly, Category="Damage", meta=(ClampMin="0.001"))
	float AttackCoefficient = 2.0f;

	/** Reusable path presentation policy. GameplayEffect classes own cue tags and lifetime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Presentation")
	TObjectPtr<UPathCombatPresentationDataAsset> PathPresentation = nullptr;

	UPROPERTY() TObjectPtr<UAbilityTask_BossSlashDashExecution> ExecutionTask;

	// Serialized compatibility for BPGA_SlashDash assets authored before MontageConfig.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.Montage."))
	TObjectPtr<UAnimMontage> DashMontage_DEPRECATED = nullptr;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.WindupEnterSectionName."))
	FName WindupSectionName_DEPRECATED = TEXT("Windup");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.AttackSectionName."))
	FName DashSlashSectionName_DEPRECATED = TEXT("DashSlash");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.TravelHoldSectionName."))
	FName DashHoldSectionName_DEPRECATED = TEXT("DashHold");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.RecoverySectionName."))
	FName RecoverySectionName_DEPRECATED = TEXT("Recover");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.WindupHoldDuration."))
	float WindupDuration_DEPRECATED = 0.5f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.RecoveryTimeout."))
	float RecoveryTimeout_DEPRECATED = 1.5f;

};
