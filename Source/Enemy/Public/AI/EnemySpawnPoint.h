// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemySpawnPoint.generated.h"

class USceneComponent;
class UArrowComponent;
class AEnemyPathActor;

UCLASS()
class ENEMY_API AEnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AEnemySpawnPoint();

protected:
	virtual void BeginPlay() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawn Point")
	TObjectPtr<USceneComponent> Root;

	/** 에디터에서 스폰 방향 확인용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawn Point")
	TObjectPtr<UArrowComponent> Arrow;

	/** SpawnPoint가 사용할 경로 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Spawn Point")
	TObjectPtr<AEnemyPathActor> AssignedPath;

	/** Path 상에서 스폰 시작 거리 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Spawn Point", meta = (ClampMin = "0.0"))
	float StartDistanceAlongPath = 0.0f;

public:
	
	// Getters - BlueprintPure
	/** 연결된 Path 반환 */
	UFUNCTION(BlueprintPure, Category = "Spawn Point")
	AEnemyPathActor* GetAssignedPath() const { return AssignedPath; }
	
	/** Path 기준으로 보정된 시작 거리 반환 */
	UFUNCTION(BlueprintPure, Category = "Spawn Point")
	float GetClampedStartDistance() const;

	/** 실제 적을 생성할 때 사용할 Transform 반환 */
	UFUNCTION(BlueprintPure, Category = "Spawn Point")
	FTransform GetSpawnTransform() const;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif
};