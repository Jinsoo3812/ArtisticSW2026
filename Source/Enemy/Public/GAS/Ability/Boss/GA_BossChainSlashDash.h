#pragma once

#include "CoreMinimal.h"
#include "BossAI/BossDeckPointSelector.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GA_BossChainSlashDash.generated.h"

class UAbilityTask_BossTargetSnapshot;
class UAbilityTask_BossVanishRelocation;

USTRUCT(BlueprintType)
struct ENEMY_API FChainSlashDashStepConfig
{
	GENERATED_BODY()
	/** First step: total reveal-to-dash time including Windup. Continuations dash immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chain Slash Dash", meta = (ClampMin = "0.0", Units = "s"))
	float WaitBeforeDash = 2.f;
	/** Extension past the player along the Samurai-to-player line, independent of player facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chain Slash Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float DistanceBeyondPlayer = 400.f;
};

/** Owns the sequence only. Snapshot, relocation and individual dash tasks own their lifetimes. */
UCLASS()
class ENEMY_API UGA_BossChainSlashDash : public UGA_BossDashSlash
{
	GENERATED_BODY()
public:
	UGA_BossChainSlashDash();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
protected:
	virtual void HandleExecutionCompleted() override;
	virtual void HandleExecutionFailed() override;
	void StartNextStep();
	void FinishSequence(bool bCancelled);
	UFUNCTION() void OnRevealed();
	UFUNCTION() void OnRelocationFailed();
	UFUNCTION() void OnDeparture(FVector Location);
	UFUNCTION() void OnArrival(FVector Location);

	/** Number of entries is the number of vanish + dash cycles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Chain Slash Dash")
	TArray<FChainSlashDashStepConfig> Steps;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Chain Slash Dash")
	FBossDestinationSelectionSettings SelectionSettings;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish")
	TObjectPtr<UAnimMontage> PreparationMontage;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.0", Units = "s"))
	float PreparationDelay = 0.45f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float HiddenDuration = 0.35f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish", meta = (ClampMin = "0.01", Units = "s"))
	float RelocationSettleTime = 0.1f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish|Feedback", meta = (Categories = "GameplayCue"))
	FGameplayTag DepartureGameplayCueTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Vanish|Feedback", meta = (Categories = "GameplayCue"))
	FGameplayTag ArrivalGameplayCueTag;
private:
	bool ValidateSequenceConfig(FString& OutError) const;
	void CancelForPointFailure(const TCHAR* Reason);
	UPROPERTY() TObjectPtr<UAbilityTask_BossTargetSnapshot> TargetSnapshotTask;
	UPROPERTY() TObjectPtr<UAbilityTask_BossVanishRelocation> RelocationTask;
	TWeakObjectPtr<AEnemyShip> SequenceShip;
	FTimerHandle NextStepTimer;
	int32 StepIndex = 0;
	bool bConsumedSequence = false;
	bool bEndingSequence = false;
};
