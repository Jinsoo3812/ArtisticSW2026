// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "BaseHitReactionGameplayAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;

UENUM(BlueprintType)
enum class EBaseHitReactionDirection : uint8
{
	Front,
	Back,
	Left,
	Right
};

UCLASS()
class GASCORE_API UBaseHitReactionGameplayAbility : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UBaseHitReactionGameplayAbility();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	UFUNCTION()
	void OnHitReactionMontageCompleted();

	UFUNCTION()
	void OnHitReactionMontageInterrupted();

	UFUNCTION()
	void OnHitReactionMontageCancelled();

	UFUNCTION(BlueprintCallable, Category = "HitReaction")
	bool PlayHitReactionMontage(EBaseHitReactionDirection Direction);

	UFUNCTION(BlueprintPure, Category = "HitReaction")
	EBaseHitReactionDirection CalculateHitReactionDirection(const FGameplayEventData& TriggerEventData) const;

	UFUNCTION(BlueprintPure, Category = "HitReaction")
	UAnimMontage* GetHitReactionMontage(EBaseHitReactionDirection Direction) const;

	/**
	 * Called after the hit direction has been resolved and before the reaction montage starts.
	 * Native subclasses can add character-specific reaction behavior without duplicating
	 * the base activation and montage flow.
	 */
	virtual void OnHitReactionActivated(
		const FGameplayEventData& TriggerEventData,
		float DamageAmount,
		EBaseHitReactionDirection Direction);

	void FinishHitReaction(bool bWasCancelled);
	bool TryGetHitReactionSourceLocation(const FGameplayEventData& TriggerEventData, FVector& OutSourceLocation) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "HitReaction", meta = (DisplayName = "On Hit Reaction Started"))
	void K2_OnHitReactionStarted(const FGameplayEventData& TriggerEventData, float DamageAmount);

	UFUNCTION(BlueprintImplementableEvent, Category = "HitReaction", meta = (DisplayName = "On Directional Hit Reaction Started"))
	void K2_OnDirectionalHitReactionStarted(const FGameplayEventData& TriggerEventData, float DamageAmount, EBaseHitReactionDirection Direction);

	UFUNCTION(BlueprintImplementableEvent, Category = "HitReaction", meta = (DisplayName = "On Hit Reaction Finished"))
	void K2_OnHitReactionFinished(bool bWasCancelled);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage")
	TObjectPtr<UAnimMontage> FrontHitReactionMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage")
	TObjectPtr<UAnimMontage> BackHitReactionMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage")
	TObjectPtr<UAnimMontage> LeftHitReactionMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage")
	TObjectPtr<UAnimMontage> RightHitReactionMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage", meta = (ClampMin = "0.0"))
	float HitReactionMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage")
	FName HitReactionMontageStartSection = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Montage")
	bool bStopHitReactionMontageWhenAbilityEnds = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	bool bAutoFinishHitReactionWithoutMontage = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	EBaseHitReactionDirection DefaultHitReactionDirection = EBaseHitReactionDirection::Front;

	UPROPERTY(BlueprintReadOnly, Category = "HitReaction")
	EBaseHitReactionDirection CurrentHitReactionDirection = EBaseHitReactionDirection::Front;

	UPROPERTY(BlueprintReadOnly, Category = "HitReaction")
	bool bHitReactionFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "HitReaction|Montage")
	TObjectPtr<UAbilityTask_PlayMontageAndWait> HitReactionMontageTask;
};
