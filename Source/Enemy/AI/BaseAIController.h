// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BaseAIController.generated.h"

struct FAIStimulus;
class UAISenseConfig_Sight;

UCLASS()
class ENEMY_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

protected:
	// AI Sight Perception 변수
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	// AI Sight Perception이 Target을 감지했을 때 호출되는 함수
	UFUNCTION()
	void OnTargetSighted(AActor* SeenTarget, FAIStimulus Stimulus);
	

protected:
	virtual void OnPossess(APawn* PossessedPawn) override;

private:
	// 초기에 AI Sight Perception 변수를 초기화하는 함수
	void SetupPerceptionSystem();
};
