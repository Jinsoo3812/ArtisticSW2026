// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShipSwarmSubsystem.generated.h"

class AEnemyShip;

/**
 * 적 배들의 군집(Squad) 관리 및 빠른 접근을 담당하는 월드 서브시스템
 */
UCLASS()
class ENEMY_API UShipSwarmSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 배가 월드에 스폰될 때 호출하여 등록
	UFUNCTION(BlueprintCallable, Category = "Ship|Swarm")
	void RegisterShip(AEnemyShip* Ship);

	// 배가 월드에서 제거되거나 파괴될 때 호출하여 등록 해제
	UFUNCTION(BlueprintCallable, Category = "Ship|Swarm")
	void UnregisterShip(AEnemyShip* Ship);

	// 지정된 SquadID에 속한 아군 적 배의 목록을 반환 (유효하지 않은 참조 자동 정리 포함)
	UFUNCTION(BlueprintCallable, Category = "Ship|Swarm")
	TArray<AEnemyShip*> GetSquadMembers(FName SquadID);

private:
	// 군집 ID별로 배들의 약참조 목록을 보관 (댕글링 포인터 방지)
	TMap<FName, TArray<TWeakObjectPtr<AEnemyShip>>> SquadMap;
};
