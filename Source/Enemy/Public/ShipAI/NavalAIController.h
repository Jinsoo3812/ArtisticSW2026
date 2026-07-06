// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "NavalAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;

UCLASS()
class ENEMY_API ANavalAIController : public AAIController
{
	GENERATED_BODY()

public:
	ANavalAIController();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI | Perception")
	TObjectPtr<UAIPerceptionComponent> PerceptionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI | Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

protected:
	// 비헤이비어 트리를 구동할 데이터 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI | Behavior Tree")
	TObjectPtr<class UBehaviorTree> DefaultBehaviorTree;
};
