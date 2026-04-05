// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyPathActor.generated.h"


class USceneComponent;
class USplineComponent;

UCLASS()
class ENEMY_API AEnemyPathActor : public AActor
{
	GENERATED_BODY()

public:
	AEnemyPathActor();

protected:
	virtual void BeginPlay() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path")
	TObjectPtr<USplineComponent> PathSpline;

public:
	//virtual void Tick(float DeltaTime) override;
	
	/** 전체 경로 길이 반환 */
	UFUNCTION(BlueprintPure, Category = "Path")
	float GetPathLength() const;

	/** 거리값을 Clamp해서 유효 범위로 만든 값 반환 */
	UFUNCTION(BlueprintPure, Category = "Path")
	float ClampDistanceToPath(float Distance) const;

	/** 현재 거리값이 경로 범위 안에 있는지 검사 */
	UFUNCTION(BlueprintPure, Category = "Path")
	bool IsValidDistance(float Distance) const;

	/** 거리 기준 월드 위치 반환 */
	UFUNCTION(BlueprintPure, Category = "Path")
	FVector GetWorldLocationAtDistance(float Distance) const;

	/** 거리 기준 월드 회전 반환 */
	UFUNCTION(BlueprintPure, Category = "Path")
	FRotator GetWorldRotationAtDistance(float Distance) const;

	/** 거리 기준 월드 Transform 반환 */
	UFUNCTION(BlueprintPure, Category = "Path")
	FTransform GetWorldTransformAtDistance(float Distance) const;

	/** 시작 지점 Transform */
	UFUNCTION(BlueprintPure, Category = "Path")
	FTransform GetStartTransform() const;

	/** 끝 지점 Transform */
	UFUNCTION(BlueprintPure, Category = "Path")
	FTransform GetEndTransform() const;

	/** 내부 Spline 접근자 */
	UFUNCTION(BlueprintPure, Category = "Path")
	USplineComponent* GetPathSpline() const { return PathSpline; }

};
