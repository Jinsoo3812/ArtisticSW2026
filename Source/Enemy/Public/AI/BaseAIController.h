// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BaseAIController.generated.h"

struct FAIStimulus;
class UAISenseConfig_Sight;
class UBaseHealthComponent;

UCLASS()
class ENEMY_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Patrol", meta = (ClampMin = "0.0"))
	float DefaultPatrolRadius = 800.0f;

	// AI Sight Perception 변수
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	// AI Sight Perception이 Target을 감지했을 때 호출되는 함수
	UFUNCTION()
	void OnTargetSighted(AActor* SeenTarget, FAIStimulus Stimulus);

	UFUNCTION()
	void OnPerceivedTargetDeathStarted(UBaseHealthComponent* HealthComponent);
	

protected:
	virtual void OnPossess(APawn* PossessedPawn) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 초기에 AI Sight Perception 변수를 초기화하는 함수
	void SetupPerceptionSystem();

	void InitializeBlackboardValues(APawn* PossessedPawn);
	void BindPerceivedTargetDeath(AActor* TargetActor);
	void UnbindPerceivedTargetDeath();

	TWeakObjectPtr<UBaseHealthComponent> ObservedTargetHealthComponent;
};
