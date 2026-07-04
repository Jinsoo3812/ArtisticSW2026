// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_NavalDrive.generated.h"

UCLASS()
class ENEMY_API UBTTask_NavalDrive : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_NavalDrive();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

protected:
	// 블랙보드 키: 감지 대상 배
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetShipKey;

	// 블랙보드 키: 이상적인 포격/대치 거리 (Float)
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector IdealDistanceKey;

	// 적 배 설정 파라미터 (원하지 않을 경우 기본값 사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI")
	float DefaultIdealDistance = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI")
	float DangerCloseDistance = 1000.f;

	// Orbit 기동 시 시계 방향(true) 또는 반시계 방향(false)으로 돌지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI")
	bool bOrbitClockwise = true;
};
