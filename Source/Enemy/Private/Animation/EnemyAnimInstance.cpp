// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/EnemyAnimInstance.h"
#include "BaseEnemy.h"
#include "AI/BaseAIController.h"
#include "Components/BaseHealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "Ship.h"

UEnemyAnimInstance::UEnemyAnimInstance()
{
	Speed = 0.0f;
	Direction = 0.0f;
	bIsMoving = false;
	bIsFalling = false;
	bIsAccelerating = false;
	MovingSpeedThreshold = 5.0f;
	CurrentAIState = EEnemyAIState::Passive;
	bHasCombatTarget = false;
	CombatTargetDistance = 0.0f;
	CombatTargetDeltaZ = 0.0f;
	bIsDead = false;
	bIsOnShip = false;
	FootPlacementAlpha = 1.0f;
}

void UEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	CacheReferences();
}

void UEnemyAnimInstance::CacheReferences()
{
	APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn)
	{
		return;
	}

	CachedEnemy = Cast<ABaseEnemy>(OwnerPawn);
	CachedMovementComponent = OwnerPawn->FindComponentByClass<UCharacterMovementComponent>();

	if (CachedEnemy.IsValid())
	{
		CachedHealthComponent = CachedEnemy->GetHealthComponent();
		CachedAIController = Cast<ABaseAIController>(CachedEnemy->GetController());
	}
	else
	{
		CachedAIController = Cast<ABaseAIController>(OwnerPawn->GetController());
	}
}

void UEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 필요시 레퍼런스 재캐싱
	if (!CachedEnemy.IsValid() || !CachedMovementComponent.IsValid())
	{
		CacheReferences();
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn)
	{
		return;
	}

	// 1. 이동(Locomotion) 데이터 갱신
	const FVector Velocity = OwnerPawn->GetVelocity();
	const FRotator ActorRotation = OwnerPawn->GetActorRotation();

	Speed = Velocity.Size2D();
	ZVelocity = Velocity.Z;
	Direction = UKismetAnimationLibrary::CalculateDirection(Velocity, ActorRotation);
	bIsMoving = (Speed > MovingSpeedThreshold);

	if (CachedMovementComponent.IsValid())
	{
		bIsFalling = CachedMovementComponent->IsFalling();
		bIsAccelerating = (CachedMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 1.0f);

		// 점프(위로 솟구침) vs 순수 낙하(아래로 떨어짐) 구분
		bIsJumping = bIsFalling && (ZVelocity > 100.0f);
		bIsFallingDown = bIsFalling && (ZVelocity <= 100.0f);

		// 배(AShip) 위에 서 있는지 여부 판별
		UPrimitiveComponent* MovementBase = CachedMovementComponent->GetMovementBase();
		if (MovementBase)
		{
			AActor* BaseOwner = MovementBase->GetOwner();
			bIsOnShip = (BaseOwner && BaseOwner->IsA<AShip>());
		}
		else
		{
			bIsOnShip = false;
		}
	}
	else
	{
		bIsFalling = false;
		bIsJumping = false;
		bIsFallingDown = false;
		bIsAccelerating = false;
		bIsOnShip = false;
	}

	// 2. AI 및 전투(Combat) 상태 갱신
	if (!CachedAIController.IsValid() && OwnerPawn->GetController())
	{
		CachedAIController = Cast<ABaseAIController>(OwnerPawn->GetController());
	}

	if (CachedAIController.IsValid())
	{
		CurrentAIState = CachedAIController->GetEnemyState();
		AActor* CombatTarget = CachedAIController->GetCombatTarget();
		if (IsValid(CombatTarget))
		{
			bHasCombatTarget = true;
			const FVector EnemyLoc = OwnerPawn->GetActorLocation();
			const FVector TargetLoc = CombatTarget->GetActorLocation();
			CombatTargetDistance = FVector::Dist2D(EnemyLoc, TargetLoc);
			CombatTargetDeltaZ = TargetLoc.Z - EnemyLoc.Z;
		}
		else
		{
			bHasCombatTarget = false;
			CombatTargetDistance = 0.0f;
			CombatTargetDeltaZ = 0.0f;
		}
	}
	else
	{
		CurrentAIState = EEnemyAIState::Passive;
		bHasCombatTarget = false;
		CombatTargetDistance = 0.0f;
		CombatTargetDeltaZ = 0.0f;
	}

	// 3. 체력 및 사망(Health & Death) 상태 갱신
	if (CachedHealthComponent.IsValid())
	{
		bIsDead = CachedHealthComponent->IsDead();
	}
	else if (CachedEnemy.IsValid())
	{
		bIsDead = (CurrentAIState == EEnemyAIState::Dead);
	}

	// 4. Foot Placement Alpha 갱신 (공중이거나 사망 시 비활성화, 지면 접지 시 1.0)
	FootPlacementAlpha = (bIsFalling || bIsDead) ? 0.0f : 1.0f;
}

void UEnemyAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	// UE5 Worker Thread에서 안전하게 실행되는 틱 (추가적인 스레드 안전 로직 확장 가능)
}
