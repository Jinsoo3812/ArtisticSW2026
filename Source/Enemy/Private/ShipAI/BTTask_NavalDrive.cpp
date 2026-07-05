#include "ShipAI/BTTask_NavalDrive.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

UBTTask_NavalDrive::UBTTask_NavalDrive()
{
	NodeName = TEXT("Naval Drive");
	bNotifyTick = true;
	
	// 각 AI마다 독립적인 인스턴스 전용 상태 변수를 갖도록 설정
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_NavalDrive::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CurrentState = ENavalCombatState::Idle;
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

	if (!MyShip)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UStaticMeshComponent* BuoyancyRoot = MyShip->BuoyancyRoot;
	if (!BuoyancyRoot)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// --- 1. 플레이어 배 감색 및 블랙보드 TargetShip 동적 등록/해제 처리 ---
	// 기획자가 이 BTTask 노드에서 지정한 DetectionDistance 변수값을 직접 주권으로 삼아 상시 탐색 및 타겟팅 판단을 내립니다.
	AShip* PlayerShip = FindPlayerShip(MyShip->GetWorld(), MyShip);
	if (PlayerShip)
	{
		float DistToPlayer = FVector::Dist(MyShip->GetActorLocation(), PlayerShip->GetActorLocation());
		if (DistToPlayer <= DetectionDistance)
		{
			// 발견 거리 이내이면 블랙보드 타겟으로 강제 등록
			if (TargetShip != PlayerShip)
			{
				BlackboardComp->SetValueAsObject(TargetShipKey.SelectedKeyName, PlayerShip);
				TargetShip = PlayerShip;
			}
		}
		else
		{
			// 발견 거리를 벗어나면 블랙보드 타겟 해제
			if (TargetShip == PlayerShip)
			{
				BlackboardComp->ClearValue(TargetShipKey.SelectedKeyName);
				TargetShip = nullptr;
			}
		}
	}
	else
	{
		if (TargetShip != nullptr)
		{
			BlackboardComp->ClearValue(TargetShipKey.SelectedKeyName);
			TargetShip = nullptr;
		}
	}

	// 타겟이 없는 경우 강제 Idle 처리
	if (!TargetShip)
	{
		CurrentState = ENavalCombatState::Idle;
	}

	FVector SelfLoc = MyShip->GetActorLocation();
	float Distance = 0.f;
	FVector ToTargetDir = FVector::ZeroVector;

	if (TargetShip)
	{
		FVector TargetLoc = TargetShip->GetActorLocation();
		FVector ToTarget = TargetLoc - SelfLoc;
		ToTarget.Z = 0.f; // 수평 거리를 위해 Z 성분 제외
		Distance = ToTarget.Size();
		ToTargetDir = ToTarget.GetSafeNormal();
	}

	// --- 2. 완충 영역(Tolerance)을 반영한 정교한 상태 전이(State Machine) ---
	if (CurrentState == ENavalCombatState::Idle)
	{
		if (TargetShip && Distance <= DetectionDistance)
		{
			CurrentState = ENavalCombatState::Approach;
		}
	}
	else // 전투 가동 상태 중
	{
		// 타겟을 놓치거나 발견 거리를 벗어난 경우
		if (!TargetShip || Distance > DetectionDistance)
		{
			CurrentState = ENavalCombatState::Idle;
		}
		// A. 접근(Approach) 상태일 때의 전이 조건 (가까워지는 중)
		else if (CurrentState == ENavalCombatState::Approach)
		{
			// 도망 거리에 오면 즉시 후퇴
			if (Distance <= DangerCloseDistance)
			{
				CurrentState = ENavalCombatState::Retreat;
			}
			// 공전(선회) 거리에 닿으면 즉시 공전 돌입
			else if (Distance <= IdealDistance)
			{
				CurrentState = ENavalCombatState::Orbit;
			}
		}
		// B. 후퇴(Retreat) 상태일 때의 전이 조건 (멀어지는 중)
		else if (CurrentState == ENavalCombatState::Retreat)
		{
			// 공전(선회) 거리에 도달할 때까지 도망치며, 도달 즉시 공전 돌입
			if (Distance >= IdealDistance)
			{
				CurrentState = ENavalCombatState::Orbit;
			}
		}
		// C. 선회(Orbit) 상태일 때의 이탈 조건 (도망은 도망 거리로만 발동, 멀어질 때만 바깥 오프셋 적용)
		else if (CurrentState == ENavalCombatState::Orbit)
		{
			// 공전거리 + offset 보다 멀어지면 접근(추격)
			if (Distance > (IdealDistance + OrbitTolerance))
			{
				CurrentState = ENavalCombatState::Approach;
			}
			// 도망거리보다 가까워지면 후퇴 (안쪽 완충 오프셋은 폐지)
			else if (Distance <= DangerCloseDistance)
			{
				CurrentState = ENavalCombatState::Retreat;
			}
		}
	}

	// --- 3. 상태별 조타 방향(Heading) 및 기동 속도 비율 설정 ---
	FVector DesiredHeading = FVector::ZeroVector;
	float SpeedFactor = 1.0f;
	FString StateString = TEXT("Unknown");
	FColor StateColor = FColor::White;

	switch (CurrentState)
	{
	case ENavalCombatState::Idle:
		DesiredHeading = MyShip->GetActorForwardVector();
		SpeedFactor = 0.0f;
		StateString = TEXT("Idle (Waiting)");
		StateColor = FColor::Cyan;
		break;

	case ENavalCombatState::Approach:
		DesiredHeading = ToTargetDir;
		SpeedFactor = 1.0f;
		StateString = TEXT("Approach (Chasing)");
		StateColor = FColor::Orange;
		break;

	case ENavalCombatState::Orbit:
		{
			FVector TangentDir = bOrbitClockwise ? 
				FVector(-ToTargetDir.Y, ToTargetDir.X, 0.f) : 
				FVector(ToTargetDir.Y, -ToTargetDir.X, 0.f);

			float DistanceDiff = Distance - IdealDistance;
			float SteeringBias = FMath::Clamp(DistanceDiff / IdealDistance, -0.4f, 0.4f);

			DesiredHeading = (TangentDir + ToTargetDir * SteeringBias).GetSafeNormal();
			SpeedFactor = 1.0f; 
			StateString = TEXT("Orbit (Circling)");
			StateColor = FColor::Green;
		}
		break;

	case ENavalCombatState::Retreat:
		DesiredHeading = -ToTargetDir;
		SpeedFactor = 1.0f;
		StateString = TEXT("Retreat (Evading)");
		StateColor = FColor::Red;
		break;
	}

	// --- 4. 플레이어식 조작 방식으로 물리 힘 연산 통일 (W 가속, A/D 토크) ---
	FVector ShipForward = MyShip->GetActorForwardVector();
	ShipForward.Z = 0.f;
	ShipForward.Normalize();

	FVector ShipRight = MyShip->GetActorRightVector();
	ShipRight.Z = 0.f;
	ShipRight.Normalize();

	float HeadingDot = FVector::DotProduct(ShipForward, DesiredHeading);
	float RightDot = FVector::DotProduct(ShipRight, DesiredHeading);

	// A. AD 회전 토크 조타 입력 (A: 좌회전, D: 우회전)
	float TurnInput = 0.f;
	if (HeadingDot < 0.99f && CurrentState != ENavalCombatState::Idle)
	{
		TurnInput = (RightDot > 0.f) ? 1.f : -1.f;
		if (HeadingDot < -0.3f)
		{
			TurnInput *= 1.3f;
		}
	}

	// B. W 전진 추진 가속 입력 (후진 S는 없고 감속/0만 존재)
	float MoveInput = 0.f;
	if (HeadingDot > 0.0f && CurrentState != ENavalCombatState::Idle)
	{
		MoveInput = HeadingDot * SpeedFactor;
	}

	// --- 5. 물리 엔진 적용 (배율 적용) ---
	if (FMath::Abs(MoveInput) > KINDA_SMALL_NUMBER)
	{
		FVector ForceToApply = ShipForward * (MyShip->ForwardForce * ForwardForceMultiplier) * MoveInput;
		BuoyancyRoot->AddForce(ForceToApply);
	}

	if (FMath::Abs(TurnInput) > KINDA_SMALL_NUMBER)
	{
		FVector TorqueToApply = FVector(0.f, 0.f, (MyShip->TurnTorque * TurnTorqueMultiplier) * TurnInput);
		BuoyancyRoot->AddTorqueInDegrees(TorqueToApply);
	}

	// --- 6. 화면 좌상단 스크린 디버그 로그 표시 ---
	if (GEngine)
	{
		FString DebugMsg = FString::Printf(TEXT("Naval Drive: %s -> %s (Dist: %.1f / Ideal: %.1f)"), 
			*MyShip->GetName(), *StateString, Distance, IdealDistance);
		
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(MyShip->GetUniqueID()), 1.0f, StateColor, DebugMsg);
	}

	// --- 7. 디버그 원 시각화 (Z축 오프셋 적용) ---
	if (bShowDebugRanges)
	{
		UWorld* World = MyShip->GetWorld();
		if (World)
		{
			FVector CenterLoc = SelfLoc;
			CenterLoc.Z += DebugZOffset; // 바다 표면 위로 오프셋 적용

			// IdealDistance (선회 대치선 - 초록색 원)
			DrawDebugCircle(
				World,
				CenterLoc,
				IdealDistance,
				64,
				FColor::Green,
				false,
				-1.f,
				0,
				3.f,
				FVector(1.f, 0.f, 0.f),
				FVector(0.f, 1.f, 0.f),
				false
			);

			// IdealDistance + OrbitTolerance (선회 이탈 상한선 - 노란색 원)
			DrawDebugCircle(
				World,
				CenterLoc,
				IdealDistance + OrbitTolerance,
				64,
				FColor::Yellow,
				false,
				-1.f,
				0,
				1.5f,
				FVector(1.f, 0.f, 0.f),
				FVector(0.f, 1.f, 0.f),
				false
			);

			// 안쪽 오프셋(주황색 원)은 설계 변경에 따라 그리지 않음

			// DangerCloseDistance (안전 이탈선 - 빨간색 원)
			DrawDebugCircle(
				World,
				CenterLoc,
				DangerCloseDistance,
				64,
				FColor::Red,
				false,
				-1.f,
				0,
				3.f,
				FVector(1.f, 0.f, 0.f),
				FVector(0.f, 1.f, 0.f),
				false
			);

			// DetectionDistance (발견 범위 - 시안색 원)
			DrawDebugCircle(
				World,
				CenterLoc,
				DetectionDistance,
				64,
				FColor::Cyan,
				false,
				-1.f,
				0,
				2.f,
				FVector(1.f, 0.f, 0.f),
				FVector(0.f, 1.f, 0.f),
				false
			);
		}
	}
}

AShip* UBTTask_NavalDrive::FindPlayerShip(UWorld* World, APawn* AIPawn) const
{
	if (!World || !AIPawn) return nullptr;

	AShip* ClosestPlayerShip = nullptr;
	float ClosestDistance = FLT_MAX;
	FVector SelfLoc = AIPawn->GetActorLocation();

	// 월드 내의 모든 플레이어 컨트롤러를 탐색 (멀티플레이어/리슨 서버 완벽 대응)
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PlayerController = Iterator->Get();
		if (!PlayerController) continue;

		APawn* PlayerPawn = PlayerController->GetPawn();
		if (!PlayerPawn) continue;

		AShip* FoundShip = nullptr;

		// 1. 플레이어가 배에 직접 빙의(Possess)하여 운전 중인 경우
		if (AShip* ControlledShip = Cast<AShip>(PlayerPawn))
		{
			FoundShip = ControlledShip;
		}
		// 2. 플레이어 캐릭터가 배의 자식 컴포넌트로 탑승(Attach)하고 있는 상태인 경우
		else if (AActor* ParentActor = PlayerPawn->GetAttachParentActor())
		{
			if (AShip* AttachedShip = Cast<AShip>(ParentActor))
			{
				FoundShip = AttachedShip;
			}
		}

		// 가장 가까운 위치의 플레이어 배를 최종 타겟팅 대상으로 선택
		if (FoundShip)
		{
			float Dist = FVector::Dist(SelfLoc, FoundShip->GetActorLocation());
			if (Dist < ClosestDistance)
			{
				ClosestDistance = Dist;
				ClosestPlayerShip = FoundShip;
			}
		}
	}

	return ClosestPlayerShip;
}
