#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "Roll/RollTypes.h"
#include "GA_PlayerRoll.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UAnimMontage;

/**
 * Owns roll gameplay rules and lifetime while delegating locomotion presentation
 * to a replaceable execution boundary. The default executor is a root-motion
 * montage; Motion Matching can later replace Start/StopRollExecution only.
 */
UCLASS()
class CLASSFEATURE_API UGA_PlayerRoll : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_PlayerRoll();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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

	UFUNCTION(BlueprintPure, Category = "Roll")
	const FRollIntent& GetCurrentRollIntent() const { return CurrentRollIntent; }

protected:
	/** Produces the stable hand-off payload consumed by montage or Motion Matching. */
	virtual FRollIntent BuildRollIntent() const;

	/** Replace this boundary when adopting Motion Matching. */
	virtual bool StartRollExecution(const FRollIntent& RollIntent);

	/** Symmetric cleanup hook for the active animation/movement executor. */
	virtual void StopRollExecution(bool bWasCancelled);

	/** Shared terminal path for montage, Motion Matching, cancellation, and failure. */
	UFUNCTION(BlueprintCallable, Category = "Roll")
	void FinishRoll(bool bWasCancelled);

	UFUNCTION()
	void HandleInvulnerabilityBegin(FGameplayEventData Payload);

	UFUNCTION()
	void HandleInvulnerabilityEnd(FGameplayEventData Payload);

	UFUNCTION()
	void HandleRollRecovery(FGameplayEventData Payload);

	UFUNCTION()
	void HandleRollMontageCompleted();

	UFUNCTION()
	void HandleRollMontageInterrupted();

	UFUNCTION()
	void HandleRollMontageCancelled();

	void StartRollEventListeners();
	void BeginRecoveryBlendOut();
	void AddRollInvulnerability();
	void RemoveRollInvulnerability();

	UFUNCTION(BlueprintImplementableEvent, Category = "Roll", meta = (DisplayName = "On Roll Intent Resolved"))
	void K2_OnRollIntentResolved(const FRollIntent& RollIntent);

	UFUNCTION(BlueprintImplementableEvent, Category = "Roll", meta = (DisplayName = "On Roll Finished"))
	void K2_OnRollFinished(bool bWasCancelled);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll|MVP")
	TObjectPtr<UAnimMontage> RollMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll|MVP", meta = (ClampMin = "0.01"))
	float RollMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll|MVP")
	FName RollMontageStartSection = NAME_None;

	/** Blend time used when Roll Recovery returns control before montage end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll|MVP", meta = (ClampMin = "0.0", Units = "s"))
	float RollRecoveryBlendOutTime = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll|Rules")
	bool bRequireGrounded = true;

	/** Root-motion MVP faces the requested direction before montage playback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll|Rules")
	bool bRotateToIntent = true;

	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	FRollIntent CurrentRollIntent;

	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	bool bRollFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	bool bInvulnerabilityActive = false;

	/** Distinguishes an authored recovery blend from a real interruption. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	bool bRecoveryRequested = false;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> RollMontageTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> InvulnerabilityBeginTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> InvulnerabilityEndTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> RollRecoveryTask;
};
