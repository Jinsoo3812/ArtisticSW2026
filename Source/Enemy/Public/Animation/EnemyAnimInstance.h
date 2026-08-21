// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "AI/EnemyAITypes.h"
#include "EnemyAnimInstance.generated.h"

class ABaseEnemy;
class ABaseAIController;
class UCharacterMovementComponent;
class UBaseHealthComponent;

/**
 * UEnemyAnimInstance
 *
 * 적(Enemy) 캐릭터의 애니메이션 블루프린트 베이스 C++ 클래스입니다.
 * - 멀티스레드 환경에서 안전하고 가볍게 이동(Speed, Direction) 및 상태 변수를 계산합니다.
 * - ABaseAIController 및 Behavior Tree Task와의 연동을 지원합니다.
 * - 파도에 흔들리는 배(Ship) 위에서의 Foot Placement 접지 판별을 지원합니다.
 */
UCLASS()
class ENEMY_API UEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UEnemyAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** 내부 캐시 레퍼런스를 갱신합니다. */
	void CacheReferences();

	// ==========================================
	// Locomotion Variables (이동 관련 프로퍼티)
	// ==========================================

	/** 지면 기준 2D 이동 속도 (cm/s) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	float Speed = 0.0f;

	/** 액터 회전 기준 이동 방향 각도 (-180.0 ~ 180.0) - 블렌드스페이스 Direction 핀에 연결 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	float Direction = 0.0f;

	/** 실제로 이동 중인지 여부 (Speed > MovingSpeedThreshold) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	bool bIsMoving = false;

	/** 공중에 떠 있는지 / 낙하 중인지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	bool bIsFalling = false;

	/** Z축 상승/하강 속도 (cm/s, 양수면 상승, 음수면 하강) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	float ZVelocity = 0.0f;

	/** 위로 솟구치는 점프 상태 (bIsFalling && ZVelocity > 100.0f) -> Jump Start 트랜지션용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	bool bIsJumping = false;

	/** 낭떠러지/난간에서 아래로 떨어지는 순수 낙하 상태 (bIsFalling && ZVelocity <= 100.0f) -> InAir 트랜지션용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	bool bIsFallingDown = false;

	/** 가속 입력이 들어오고 있는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Movement")
	bool bIsAccelerating = false;

	/** 이동 판정 최소 속도 임계치 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Movement")
	float MovingSpeedThreshold = 5.0f;

	// ==========================================
	// AI & Combat Variables (AI 및 전투 상태)
	// ==========================================

	/** 적 AI의 현재 상위 상태 (Passive, Investigating, Combat, Dead 등) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	EEnemyAIState CurrentAIState = EEnemyAIState::Passive;

	/** 현재 인지하여 쫓고 있는 공격 대상(Player)이 존재하는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	bool bHasCombatTarget = false;

	/** 타깃(플레이어)과의 2D 평면 거리 (cm) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float CombatTargetDistance = 0.0f;

	/** 타깃(플레이어)과의 높이 차이 (Z cm) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float CombatTargetDeltaZ = 0.0f;

	/** 적이 사망했는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Combat")
	bool bIsDead = false;

	// ==========================================
	// Ship & Foot Placement (배 위 접지 지원)
	// ==========================================

	/** 현재 캐릭터가 배(AShip) 위에 탑승/접지되어 있는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Ship")
	bool bIsOnShip = false;

	/** Foot Placement 활성화 가중치 (지면에 서 있을 때 1.0, 공중이거나 사망 시 0.0) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Ship")
	float FootPlacementAlpha = 1.0f;

public:
	// ==========================================
	// Blueprint Pure Getters (Thread-Safe)
	// ==========================================
	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	float GetSpeed() const { return Speed; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	float GetDirection() const { return Direction; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	bool IsMoving() const { return bIsMoving; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	bool IsFalling() const { return bIsFalling; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	float GetZVelocity() const { return ZVelocity; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	bool IsJumping() const { return bIsJumping; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	bool IsFallingDown() const { return bIsFallingDown; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	bool IsOnShip() const { return bIsOnShip; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	EEnemyAIState GetCurrentAIState() const { return CurrentAIState; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Animation", meta = (BlueprintThreadSafe))
	float GetFootPlacementAlpha() const { return FootPlacementAlpha; }

protected:
	// ==========================================
	// Cached Object References (약참조 캐시)
	// ==========================================
	UPROPERTY(Transient)
	TWeakObjectPtr<ABaseEnemy> CachedEnemy;

	UPROPERTY(Transient)
	TWeakObjectPtr<UCharacterMovementComponent> CachedMovementComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<ABaseAIController> CachedAIController;

	UPROPERTY(Transient)
	TWeakObjectPtr<UBaseHealthComponent> CachedHealthComponent;
};
