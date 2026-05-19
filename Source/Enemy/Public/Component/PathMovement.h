// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PathMovement.generated.h"

class AEnemyPathActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPathGoalReachedSignature, AActor*, OwnerActor);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ENEMY_API UPathMovement : public UActorComponent
{
	GENERATED_BODY()

public:
	UPathMovement();

protected:
	virtual void BeginPlay() override;
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	/** 현재 따라가는 Path */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentPath, VisibleInstanceOnly, BlueprintReadOnly, Category="Path")
	TObjectPtr<AEnemyPathActor> CurrentPath = nullptr;

	/** Path 상 현재 진행 거리 (서버 authoritative) */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentDistanceAlongPath, VisibleInstanceOnly, BlueprintReadOnly, Category="Path", meta=(ClampMin="0.0"))
	float CurrentDistanceAlongPath = 0.0f;

	/** 기본 이동 속도 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category="Path", meta=(ClampMin="0.0"))
	float BaseMoveSpeed = 300.0f;

	/** 현재 이동 활성화 여부 */
	UPROPERTY(ReplicatedUsing=OnRep_PathMovementActive, VisibleInstanceOnly, BlueprintReadOnly, Category="Path")
	bool bPathMovementActive = false;

	/** Goal 도달 여부 */
	UPROPERTY(ReplicatedUsing=OnRep_ReachedGoal, VisibleInstanceOnly, BlueprintReadOnly, Category="Path")
	bool bReachedGoal = false;

	/** 클라이언트 시각 보간용 거리 */
	UPROPERTY(Transient)
	float ClientVisualDistance = 0.0f;

	/** 클라이언트가 마지막으로 받은 서버 거리 */
	UPROPERTY(Transient)
	float ClientTargetDistance = 0.0f;

	/** 클라이언트 보간 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Path|Network", meta=(ClampMin="0.0"))
	float ClientInterpolationSpeed = 12.0f;

	/** 스플라인 위치에서 Z축으로 얼마나 띄울지 (바닥에 파묻히면 올리고, 떠있으면 내리기) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Path")
	float PathZOffset = 0.0f;

public:
	UPROPERTY(BlueprintAssignable, Category="Path")
	FOnPathGoalReachedSignature OnPathGoalReached;

public:
	UFUNCTION(BlueprintCallable, Category="Path")
	void InitializePath(AEnemyPathActor* InPath, float InStartDistance);

	UFUNCTION(BlueprintCallable, Category="Path")
	void StartPathMovement();

	UFUNCTION(BlueprintCallable, Category="Path")
	void StopPathMovement();

	UFUNCTION(BlueprintCallable, Category="Path")
	void ResetPathState();

	UFUNCTION(BlueprintCallable, Category="Path")
	bool CanMoveAlongPath() const;

	UFUNCTION(BlueprintCallable, Category="Path")
	float GetCurrentMoveSpeed() const;

	UFUNCTION(BlueprintCallable, Category="Path")
	void UpdatePathMovement(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category="Path")
	void HandleReachedGoal();

	UFUNCTION(BlueprintPure, Category="Path")
	AEnemyPathActor* GetCurrentPath() const { return CurrentPath; }

	UFUNCTION(BlueprintPure, Category="Path")
	float GetCurrentDistanceAlongPath() const { return CurrentDistanceAlongPath; }

	UFUNCTION(BlueprintPure, Category="Path")
	bool HasReachedGoal() const { return bReachedGoal; }

	UFUNCTION(BlueprintPure, Category="Path")
	bool IsPathMovementActive() const { return bPathMovementActive; }

	UFUNCTION(BlueprintCallable, Category="Path")
	void SetBaseMoveSpeed(float NewSpeed);

protected:
	UFUNCTION()
	void OnRep_CurrentPath();

	UFUNCTION()
	void OnRep_CurrentDistanceAlongPath();

	UFUNCTION()
	void OnRep_PathMovementActive();

	UFUNCTION()
	void OnRep_ReachedGoal();

protected:
	void ApplyTransformFromCurrentDistance();
	void ApplyTransformFromDistance(float DistanceAlongPath);
	void SmoothReplicatedMovement(float DeltaTime);
	void UpdateComponentTickState();
};