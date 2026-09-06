// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/EnemyAITypes.h"
#include "AI/EnemyPerceptionSettings.h"
#include "BaseAIController.generated.h"

struct FAIStimulus;
class ABaseEnemy;
class UAISenseConfig_Damage;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UBaseHealthComponent;
class UEnemyBehaviorSet;

UCLASS()
class ENEMY_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	EEnemyAIState GetEnemyState() const;

	/** The only public entry point for changing the high-level Blackboard state. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	bool SetEnemyState(EEnemyAIState NewState);

	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	bool SetCombatTarget(AActor* TargetActor);

	/** Returns the controller-owned combat target used by Blackboard gameplay and EQS contexts. */
	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	AActor* GetCombatTarget() const;

	/**
	 * Supplies a target to EQS preview tools without starting combat or requiring a Blackboard.
	 * Passing nullptr clears the preview target. Runtime combat code should use SetCombatTarget instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI|EQS")
	void SetEQSPreviewTarget(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	void ClearCombatTarget(bool bReturnToPassive = true);

	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	bool StartInvestigation(const FVector& PointOfInterest);

	/** Reapply the possessed enemy's dynamic subtrees after pooled brain logic resumes. */
	bool RefreshBehaviorRouting();

protected:
	// 주로 Player 인식한 공격 대상
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
	FName TargetActorKeyName = TEXT("TargetActor");

	// 현재 Enemy 자신의 위치
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
	FName HomeLocationKeyName = TEXT("HomeLocation");

	// 랜덤성을 부여할 반지름
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
	FName PatrolRadiusKeyName = TEXT("PatrolRadius");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
	FName StateKeyName = TEXT("State");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
	FName PointOfInterestKeyName = TEXT("PointOfInterest");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Patrol", meta = (ClampMin = "0.0"))
	float DefaultPatrolRadius = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Targeting", meta = (ClampMin = "0.0"))
	float TargetReacquireDelay = 0.25f;

	/** Weak runtime cache shared by combat code and EQS contexts. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CachedTargetActor;

	// Runtime-only sense configurations. Tuning is owned by the possessed Enemy BP.
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;

	// One dispatcher receives Sight, Hearing, and Damage stimuli.
	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* SensedActor, FAIStimulus Stimulus);

	UFUNCTION()
	void OnPerceivedTargetDeathStarted(UBaseHealthComponent* HealthComponent);

	UFUNCTION()
	void OnPossessedEnemyDeathStarted(UBaseHealthComponent* HealthComponent);

	virtual bool IsValidPerceptionTarget(const AActor* Candidate) const;
	virtual void HandleSightStimulus(AActor* SensedActor, const FAIStimulus& Stimulus);
	virtual void HandleHearingStimulus(AActor* SensedActor, const FAIStimulus& Stimulus);
	virtual void HandleDamageStimulus(AActor* SensedActor, const FAIStimulus& Stimulus);

protected:
	virtual void OnPossess(APawn* PossessedPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Initializes read-only runtime components. Per-enemy values live on the Pawn BP.
	void SetupPerceptionSystem();
	void RefreshPerceptionConfiguration(const FEnemyPerceptionSettings& Settings);

	void InitializeBlackboardValues(APawn* PossessedPawn);
	void ApplyBehaviorSet(const UEnemyBehaviorSet* BehaviorSet);
	AActor* SelectBestPerceivedTarget() const;
	void TryReacquireCombatTarget();
	void BindPerceivedTargetDeath(AActor* TargetActor);
	void UnbindPerceivedTargetDeath();
	void BindPossessedEnemyDeath(ABaseEnemy* PossessedEnemy);
	void UnbindPossessedEnemyDeath();

	TWeakObjectPtr<UBaseHealthComponent> ObservedTargetHealthComponent;
	TWeakObjectPtr<UBaseHealthComponent> PossessedEnemyHealthComponent;
	FTimerHandle TargetReacquireTimerHandle;
};
