// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/EnemyAITypes.h"
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

	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	void ClearCombatTarget(bool bReturnToPassive = true);

	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	bool StartInvestigation(const FVector& PointOfInterest);

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

	// Internal runtime sense configurations. Blueprint defaults must be authored
	// through the normalized AI|Perception properties below, not the component.
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Sight", meta = (ClampMin = "0.0"))
	float SightRadius = 2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Sight", meta = (ClampMin = "0.0"))
	float LoseSightRadius = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Sight", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float PeripheralVisionDegrees = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Sight", meta = (ClampMin = "0.0"))
	float SightMaxAge = 3.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Sight", meta = (ClampMin = "0.0"))
	float AutoSuccessRangeFromLastSeenLocation = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Hearing", meta = (ClampMin = "0.0"))
	float HearingRange = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Hearing", meta = (ClampMin = "0.0"))
	float HearingMaxAge = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception|Damage", meta = (ClampMin = "0.0"))
	float DamageMaxAge = 5.0f;

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
	// Initializes the read-only runtime component. Per-archetype values are authored
	// exclusively through the Controller's AI|Perception defaults.
	void SetupPerceptionSystem();
	void RefreshPerceptionConfiguration();

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
