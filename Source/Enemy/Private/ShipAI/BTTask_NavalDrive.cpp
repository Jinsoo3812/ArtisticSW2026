#include "ShipAI/BTTask_NavalDrive.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "ShipAI/NavalAIController.h"

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

	// --- 1. 리플렉션을 사용해 BP_EnemyShip 인스턴스 변수 다이렉트 동적 로드 ---
	// 블랙보드나 C++ 소속의 꼬임을 원천 예방하고 기획자가 블루프린트 디테일창에서 세팅한 값을 100% 무조건 정확하게 연동합니다.
	// (UE5의 Large World Coordinates에 따른 Double/Float 크기 차이를 완벽 소화하도록 FNumericProperty 방식을 채택합니다)
	UClass* PawnClass = MyShip->GetClass();
	FProperty* ReturnPointProp = PawnClass->FindPropertyByName(TEXT("ReturnPointActor"));
	FProperty* ReturnOffsetProp = PawnClass->FindPropertyByName(TEXT("ReturnArrivalOffset"));

	AActor* ReturnPointActor = nullptr;
	float ReturnArrivalOffset = 800.f; // 기본 폴백값

	if (ReturnPointProp)
	{
		if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(ReturnPointProp))
		{
			UObject* ObjValue = ObjectProp->GetObjectPropertyValue(ReturnPointProp->ContainerPtrToValuePtr<void>(MyShip));
			ReturnPointActor = Cast<AActor>(ObjValue);
		}
	}
	if (ReturnOffsetProp)
	{
		if (FNumericProperty* NumericProp = CastField<FNumericProperty>(ReturnOffsetProp))
		{
			ReturnArrivalOffset = static_cast<float>(NumericProp->GetFloatingPointPropertyValue(ReturnOffsetProp->ContainerPtrToValuePtr<void>(MyShip)));
		}
	}
	if (ReturnArrivalOffset <= 0.f)
	{
		ReturnArrivalOffset = 800.f;
	}

	UStaticMeshComponent* BuoyancyRoot = MyShip->BuoyancyRoot;
	if (!BuoyancyRoot)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// --- 2. 플레이어 배 감색 및 블랙보드 TargetShip 동적 등록/해제 처리 ---
	AShip* PlayerShip = FindPlayerShip(MyShip->GetWorld(), MyShip);
	if (PlayerShip)
	{
		float DistToPlayer = FVector::Dist2D(MyShip->GetActorLocation(), PlayerShip->GetActorLocation());
		if (DistToPlayer <= DetectionDistance)
		{
			// 발견 거리 이내이면 블랙보드 타겟으로 등록
			if (TargetShip != PlayerShip)
			{
				BlackboardComp->SetValueAsObject(TargetShipKey.SelectedKeyName, PlayerShip);
				TargetShip = PlayerShip;
			}
		}
		else
		{
			// 발견 거리를 벗어나면 블랙보드 타겟 해제 -> Return 상태로 넘어가 기지로 복귀함
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

	// 타겟과 복귀 위치 정보 사전 산출
	FVector SelfLoc = MyShip->GetActorLocation();
	
	// A. 타겟 거리 및 벡터 계산
	float Distance = 0.f;
	FVector ToTargetDir = FVector::ZeroVector;
	if (TargetShip)
	{
		FVector TargetLoc = TargetShip->GetActorLocation();
		FVector ToTarget = TargetLoc - SelfLoc;
		ToTarget.Z = 0.f; // 수평 좌표만 사용
		Distance = ToTarget.Size();
		ToTargetDir = ToTarget.GetSafeNormal();
	}

	// B. 복귀 위치 거리 및 벡터 계산
	float DistToReturn = 0.f;
	FVector ToReturnDir = FVector::ZeroVector;
	if (ReturnPointActor)
	{
		FVector ReturnLoc = ReturnPointActor->GetActorLocation();
		DistToReturn = FVector::Dist2D(SelfLoc, ReturnLoc);
		
		FVector ToReturn = ReturnLoc - SelfLoc;
		ToReturn.Z = 0.f;
		ToReturnDir = ToReturn.GetSafeNormal();
	}

	// --- 3. 귀환(Return) 상태를 통합한 기획 행동 상태 전이 (State Machine) ---
	if (CurrentState == ENavalCombatState::Idle)
	{
		// 대기 중 플레이어가 감지 범위 내로 접근 시 즉시 추격
		if (TargetShip && Distance <= DetectionDistance)
		{
			CurrentState = ENavalCombatState::Approach;
		}
		// 가만히 대기 중이어도 복귀 지점 밖으로 벗어난 경우에는 복귀 기동 작동
		else if (ReturnPointActor && DistToReturn > ReturnArrivalOffset)
		{
			CurrentState = ENavalCombatState::Return;
		}
	}
	else if (CurrentState == ENavalCombatState::Return)
	{
		// 복귀 항해 중이더라도 플레이어 배가 발견 거리 안으로 들어오면 즉각 전투 개시
		if (TargetShip && Distance <= DetectionDistance)
		{
			CurrentState = ENavalCombatState::Approach;
		}
		// 복귀 목적지 범위 내로 도달하면 전진을 멈추고 Idle 주차 상태로 전이
		else if (DistToReturn <= ReturnArrivalOffset)
		{
			CurrentState = ENavalCombatState::Idle;
		}
	}
	else // 현재 전투 기동 중 (Approach, Orbit, Retreat)
	{
		// 플레이어 타겟을 놓치거나 발견 거리 밖으로 완전히 멀어지게 된 경우
		if (!TargetShip || Distance > DetectionDistance)
		{
			if (ReturnPointActor)
			{
				CurrentState = ENavalCombatState::Return; // 복귀 시작
			}
			else
			{
				CurrentState = ENavalCombatState::Idle; // 폴백 제자리 대기
			}
		}
		// A. 접근(Approach) 상태 전이 (가까워지는 중)
		else if (CurrentState == ENavalCombatState::Approach)
		{
			if (Distance <= DangerCloseDistance)
			{
				CurrentState = ENavalCombatState::Retreat;
			}
			else if (Distance <= IdealDistance)
			{
				CurrentState = ENavalCombatState::Orbit;
			}
		}
		// B. 후퇴(Retreat) 상태 전이 (멀어지는 중)
		else if (CurrentState == ENavalCombatState::Retreat)
		{
			if (Distance >= IdealDistance)
			{
				CurrentState = ENavalCombatState::Orbit;
			}
		}
		// C. 선회(Orbit) 상태 전이 (완충 오프셋 적용)
		else if (CurrentState == ENavalCombatState::Orbit)
		{
			if (Distance > (IdealDistance + OrbitTolerance))
			{
				CurrentState = ENavalCombatState::Approach;
			}
			else if (Distance <= DangerCloseDistance)
			{
				CurrentState = ENavalCombatState::Retreat;
			}
		}
	}

	// --- 4. 상태별 조타 방향(Heading) 및 기동 속도 비율 설정 ---
	FVector DesiredHeading = FVector::ZeroVector;
	float SpeedFactor = 1.0f;
	FString StateString = TEXT("Unknown");
	FColor StateColor = FColor::White;

	switch (CurrentState)
	{
	case ENavalCombatState::Idle:
		DesiredHeading = MyShip->GetActorForwardVector();
		SpeedFactor = 0.0f; // 가속 0
		if (ReturnPointActor)
		{
			StateString = FString::Printf(TEXT("Idle (At %s / Dist: %.1f <= %.1f)"), 
				*ReturnPointActor->GetName(), DistToReturn, ReturnArrivalOffset);
			StateColor = FColor::Blue;
		}
		else
		{
			StateString = TEXT("Idle (No Base)");
			StateColor = FColor::Cyan;
		}
		break;

	case ENavalCombatState::Return:
		DesiredHeading = ToReturnDir;
		SpeedFactor = 1.0f; // 전속 복귀 가속
		StateString = FString::Printf(TEXT("Return -> %s (Dist: %.1f / Offset: %.1f)"), 
			*ReturnPointActor->GetName(), DistToReturn, ReturnArrivalOffset);
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

	// --- 5. 플레이어식 조작 방식으로 물리 힘 연산 통일 (W 가속, A/D 토크) ---
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
	if (HeadingDot < 0.99f && SpeedFactor > KINDA_SMALL_NUMBER)
	{
		TurnInput = (RightDot > 0.f) ? 1.f : -1.f;
		if (HeadingDot < -0.3f)
		{
			TurnInput *= 1.3f;
		}
	}

	// B. W 전진 추진 가속 입력 (후진 S는 없고 감속/0만 존재)
	float MoveInput = 0.f;
	if (HeadingDot > 0.0f && SpeedFactor > KINDA_SMALL_NUMBER)
	{
		MoveInput = HeadingDot * SpeedFactor;
	}

	// --- 6. 물리 엔진 적용 (배율 적용) ---
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

	// --- 7. 화면 좌상단 스크린 디버그 로그 표시 ---
	if (GEngine)
	{
		FString DebugMsg = FString::Printf(TEXT("Naval Drive: %s -> %s"), 
			*MyShip->GetName(), *StateString);
		
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(MyShip->GetUniqueID()), 1.0f, StateColor, DebugMsg);
	}

	// --- 8. 디버그 원 시각화 (Z축 오프셋 적용) ---
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

			// Return Base Visualizer (귀환 지점이 지정되어 있다면 파란색 원 및 경로선 표시)
			if (ReturnPointActor)
			{
				FVector ReturnLoc = ReturnPointActor->GetActorLocation();
				ReturnLoc.Z = CenterLoc.Z; // 동일한 높이로 띄움

				// 도달 반경 원 (파란색 원)
				DrawDebugCircle(
					World,
					ReturnLoc,
					ReturnArrivalOffset,
					32,
					FColor::Blue,
					false,
					-1.f,
					0,
					2.5f,
					FVector(1.f, 0.f, 0.f),
					FVector(0.f, 1.f, 0.f),
					false
				);

				// 귀환 기동 경로 가이드라인 (얇은 파란색 실선)
				DrawDebugLine(
					World,
					CenterLoc,
					ReturnLoc,
					FColor::Blue,
					false,
					-1.f,
					0,
					1.5f
				);
			}
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
