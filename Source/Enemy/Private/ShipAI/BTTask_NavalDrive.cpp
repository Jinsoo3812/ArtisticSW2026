// Fill out your copyright notice in the Description page of Project Settings.


#include "ShipAI/BTTask_NavalDrive.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"
#include "Components/StaticMeshComponent.h"

UBTTask_NavalDrive::UBTTask_NavalDrive()
{
	NodeName = TEXT("Naval Drive");
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_NavalDrive::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::InProgress;
}

void UBTTask_NavalDrive::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* MyController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!MyController || !BlackboardComp)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AShip* MyShip = Cast<AShip>(MyController->GetPawn());
	AShip* TargetShip = Cast<AShip>(BlackboardComp->GetValueAsObject(TargetShipKey.SelectedKeyName));

	if (!MyShip || !TargetShip)
	{
		// 조종할 배나 타겟이 없으면 실패 처리
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UStaticMeshComponent* BuoyancyRoot = MyShip->BuoyancyRoot;
	if (!BuoyancyRoot)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// 이상적인 거리 값 가져오기
	float IdealDistance = DefaultIdealDistance;
	if (IdealDistanceKey.SelectedKeyType != nullptr)
	{
		float CustomDistance = BlackboardComp->GetValueAsFloat(IdealDistanceKey.SelectedKeyName);
		if (CustomDistance > 0.f)
		{
			IdealDistance = CustomDistance;
		}
	}

	FVector SelfLoc = MyShip->GetActorLocation();
	FVector TargetLoc = TargetShip->GetActorLocation();

	// 수평 벡터(XY 평면) 거리 및 방향 계산
	FVector ToTarget = TargetLoc - SelfLoc;
	ToTarget.Z = 0.f;
	float Distance = ToTarget.Size();
	FVector ToTargetDir = ToTarget.GetSafeNormal();

	FVector DesiredHeading = FVector::ZeroVector;
	float SpeedFactor = 1.0f;

	// --- 1. 거리에 따른 전술 기동 방향 결정 ---
	if (Distance > IdealDistance + 400.f)
	{
		// 너무 멂: 플레이어 배를 향해 돌진 (Approach)
		DesiredHeading = ToTargetDir;
	}
	else if (Distance < DangerCloseDistance)
	{
		// 너무 가까움: 도망치거나 거리를 빠르게 벌림 (Retreat)
		DesiredHeading = -ToTargetDir;
	}
	else
	{
		// 적정 거리: 플레이어 주변을 도는 선회 기동 (Orbit / Broadside)
		// 90도 꺾은 접선 벡터 계산
		FVector TangentDir = bOrbitClockwise ? 
			FVector(-ToTargetDir.Y, ToTargetDir.X, 0.f) : 
			FVector(ToTargetDir.Y, -ToTargetDir.X, 0.f);

		// 거리가 오차범위에 있을 때 안팎으로 나선 조정을 위한 바이어스 계산
		float DistanceDiff = Distance - IdealDistance;
		float SteeringBias = FMath::Clamp(DistanceDiff / IdealDistance, -0.4f, 0.4f);

		// 접선과 플레이어 방향(안/밖)을 보간하여 목표 방향 결정
		DesiredHeading = (TangentDir + ToTargetDir * SteeringBias).GetSafeNormal();
	}

	// --- 2. 물리 조타 계산 (AddForce / AddTorque) ---
	FVector ShipForward = MyShip->GetActorForwardVector();
	ShipForward.Z = 0.f;
	ShipForward.Normalize();

	FVector ShipRight = MyShip->GetActorRightVector();
	ShipRight.Z = 0.f;
	ShipRight.Normalize();

	float HeadingDot = FVector::DotProduct(ShipForward, DesiredHeading);
	float RightDot = FVector::DotProduct(ShipRight, DesiredHeading);

	// A. 회전 입력 (Turn Input) 계산
	float TurnInput = 0.f;
	if (HeadingDot < 0.99f)
	{
		// 우측 내적이 양수이면 우회전(1.f), 음수이면 좌회전(-1.f)
		TurnInput = (RightDot > 0.f) ? 1.f : -1.f;

		// 뱃머리가 반대 방향에 가까울 때 신속한 선회를 위해 입력 가중
		if (HeadingDot < -0.3f)
		{
			TurnInput *= 1.3f;
		}
	}

	// B. 전진 추진력 (Move Input) 계산
	float MoveInput = 0.f;
	// 뱃머리가 원하는 목표 방향과 어느정도 정렬되었을 때만 추진 (정렬 중 불필요한 관성 이동 방지)
	if (HeadingDot > 0.3f)
	{
		MoveInput = HeadingDot * SpeedFactor;
	}

	// --- 3. 물리 엔진 적용 ---
	// AShip에 정의된 기본 이동 파라미터를 사용합니다.
	if (FMath::Abs(MoveInput) > KINDA_SMALL_NUMBER)
	{
		FVector ForceToApply = ShipForward * MyShip->ForwardForce * MoveInput;
		BuoyancyRoot->AddForce(ForceToApply);
	}

	if (FMath::Abs(TurnInput) > KINDA_SMALL_NUMBER)
	{
		FVector TorqueToApply = FVector(0.f, 0.f, MyShip->TurnTorque * TurnInput);
		BuoyancyRoot->AddTorqueInDegrees(TorqueToApply);
	}
}
