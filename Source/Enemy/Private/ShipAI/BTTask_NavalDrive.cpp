#include "ShipAI/BTTask_NavalDrive.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "ShipAI/NavalAIController.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowNavalAIDebug(
		TEXT("p.ShowNavalAIDebug"),
		0,
		TEXT("Draw naval AI on-screen and range diagnostics. 0=off, 1=on."),
		ECVF_Cheat);
}

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
	if (AAIController* Controller = OwnerComp.GetAIOwner())
	{
		if (AShip* Ship = Cast<AShip>(Controller->GetPawn()))
		{
			Ship->SetAIControlInput(0.0f, 0.0f);
		}
	}
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_NavalDrive::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
	{
		if (AShip* Ship = Cast<AShip>(Controller->GetPawn()))
		{
			Ship->SetAIControlInput(0.0f, 0.0f);
		}
	}

	return EBTNodeResult::Aborted;
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
	/* Enemy Ship Network Physics state-transition diagnostic value disabled after validation.
	const ENavalCombatState StateAtTickStart = CurrentState;
	*/

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

	// 배의 인스턴스 전용 세팅값 동적 조회
	float CurrentIdealDistance = IdealDistance;
	FName CurrentSquadID = NAME_None;
	if (AEnemyShip* EnemyShip = Cast<AEnemyShip>(MyShip))
	{
		CurrentIdealDistance = EnemyShip->IdealDistance;
		CurrentSquadID = EnemyShip->SquadID;
	}
	
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
			else if (Distance <= CurrentIdealDistance)
			{
				CurrentState = ENavalCombatState::Orbit;
			}
		}
		// B. 후퇴(Retreat) 상태 전이 (멀어지는 중)
		else if (CurrentState == ENavalCombatState::Retreat)
		{
			if (Distance >= CurrentIdealDistance)
			{
				CurrentState = ENavalCombatState::Orbit;
			}
		}
		// C. 선회(Orbit) 상태 전이 (완충 오프셋 적용)
		else if (CurrentState == ENavalCombatState::Orbit)
		{
			if (Distance > (CurrentIdealDistance + OrbitTolerance))
			{
				CurrentState = ENavalCombatState::Approach;
			}
			else if (Distance <= DangerCloseDistance)
			{
				CurrentState = ENavalCombatState::Retreat;
			}
		}
	}

	// --- 4. 상태별 기본 조타 방향(BaseHeading) 및 기동 속도 비율 설정 ---
	FVector BaseHeading = FVector::ZeroVector;
	float SpeedFactor = 1.0f;
	FString StateString = TEXT("Unknown");
	FColor StateColor = FColor::White;

	switch (CurrentState)
	{
	case ENavalCombatState::Idle:
		BaseHeading = MyShip->GetActorForwardVector();
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
		BaseHeading = ToReturnDir;
		SpeedFactor = 1.0f; // 전속 복귀 가속
		StateString = FString::Printf(TEXT("Return -> %s (Dist: %.1f / Offset: %.1f)"), 
			*ReturnPointActor->GetName(), DistToReturn, ReturnArrivalOffset);
		StateColor = FColor::Cyan;
		break;

	case ENavalCombatState::Approach:
		BaseHeading = ToTargetDir;
		SpeedFactor = 1.0f;
		StateString = TEXT("Approach (Chasing)");
		StateColor = FColor::Orange;
		break;

	case ENavalCombatState::Orbit:
		{
			FVector TangentDir = bOrbitClockwise ? 
				FVector(-ToTargetDir.Y, ToTargetDir.X, 0.f) : 
				FVector(ToTargetDir.Y, -ToTargetDir.X, 0.f);

			float DistanceDiff = Distance - CurrentIdealDistance;
			float SteeringBias = FMath::Clamp(DistanceDiff / CurrentIdealDistance, -0.4f, 0.4f);

			BaseHeading = (TangentDir + ToTargetDir * SteeringBias).GetSafeNormal();
			SpeedFactor = 1.0f; 
			StateString = FString::Printf(TEXT("Orbit (Ideal Dist: %.1f)"), CurrentIdealDistance);
			StateColor = FColor::Green;
		}
		break;

	case ENavalCombatState::Retreat:
		BaseHeading = -ToTargetDir;
		SpeedFactor = 1.0f;
		StateString = TEXT("Retreat (Evading)");
		StateColor = FColor::Red;
		break;
	}

	// --- 4.5. 컨텍스트 스티어링 (Context Steering) 적용 ── 서버 10Hz 의사결정 주기 ---
	FVector DesiredHeading = BaseHeading;
	if (MyShip->HasAuthority() && CurrentState != ENavalCombatState::Idle)
	{
		double CurrentTime = MyShip->GetWorld()->GetTimeSeconds();
		if (CurrentTime - LastDecisionTime >= 0.1 || CachedDesiredHeading.IsNearlyZero())
		{
			LastDecisionTime = CurrentTime;

			// A. 12방향 후보 레이(Ray) 정적 벡터 (런타임 Cos/Sin 연산 부하 0)
			const int32 NumRays = 12;
			static const FVector Rays[NumRays] = {
				FVector(1.000000f, 0.000000f, 0.f),   // 0도 (전방)
				FVector(0.866025f, 0.500000f, 0.f),   // 30도
				FVector(0.500000f, 0.866025f, 0.f),   // 60도
				FVector(0.000000f, 1.000000f, 0.f),   // 90도 (우측)
				FVector(-0.500000f, 0.866025f, 0.f),  // 120도
				FVector(-0.866025f, 0.500000f, 0.f),  // 150도
				FVector(-1.000000f, 0.000000f, 0.f),  // 180도 (후방)
				FVector(-0.866025f, -0.500000f, 0.f), // 210도
				FVector(-0.500000f, -0.866025f, 0.f), // 240도
				FVector(-0.000000f, -1.000000f, 0.f), // 270도 (좌측)
				FVector(0.500000f, -0.866025f, 0.f),  // 300도
				FVector(0.866025f, -0.500000f, 0.f)   // 330도
			};

			// B. 관심도(Interest) 산출: 원래 이동하고자 하는 BaseHeading과의 방향 일치성 (0.0 ~ 1.0)
			float Interest[NumRays];
			for (int32 i = 0; i < NumRays; ++i)
			{
				float Dot = FVector::DotProduct(Rays[i], BaseHeading);
				Interest[i] = FMath::Max(0.f, Dot);
			}

			// C. 위험도(Danger) 산출: 같은 군집 내의 동료 배들로부터 회피
			float Danger[NumRays];
			FMemory::Memzero(Danger, sizeof(float) * NumRays);

			if (UShipSwarmSubsystem* SwarmSubsystem = MyShip->GetWorld()->GetSubsystem<UShipSwarmSubsystem>())
			{
				TArray<AEnemyShip*> SquadMembers = SwarmSubsystem->GetSquadMembers(CurrentSquadID);
				
				// 내 배의 실제 크기 (바운딩 박스 기준 절반 길이) 구하기
				float MySize = MyShip->BuoyancyRoot ? MyShip->BuoyancyRoot->Bounds.BoxExtent.GetMax() : 500.f;

				for (AEnemyShip* Member : SquadMembers)
				{
					if (!Member || Member == MyShip) continue;

					FVector ToOther = Member->GetActorLocation() - SelfLoc;
					ToOther.Z = 0.f;
					float Dist = ToOther.Size();

					// 상대방 배의 실제 크기 구하기
					float MemberSize = Member->BuoyancyRoot ? Member->BuoyancyRoot->Bounds.BoxExtent.GetMax() : 500.f;
					
					// 동적 회피 반경 = 내 크기 + 상대 크기 + 안전 버퍼(800.f)
					float AvoidanceRadius = MySize + MemberSize + 800.f;

					if (Dist < AvoidanceRadius && Dist > 1.f)
					{
						FVector ToOtherDir = ToOther.GetSafeNormal();
						// 가까울수록 비선형적으로 점수가 급증하도록 제곱 사용
						float DangerWeight = FMath::Square(1.0f - (Dist / AvoidanceRadius));

						// A. 기본 물리적 회피 위험도 적용
						for (int32 i = 0; i < NumRays; ++i)
						{
							float Dot = FVector::DotProduct(Rays[i], ToOtherDir);
							if (Dot > 0.f)
							{
								Danger[i] = FMath::Max(Danger[i], Dot * DangerWeight);
							}
						}

						// B. 정면 마주침 대칭성 깨기 보정 (COLREGs 우현 선회 원칙 적용)
						FVector ShipForward = MyShip->GetActorForwardVector();
						ShipForward.Z = 0.f;
						ShipForward.Normalize();

						float ForwardDot = FVector::DotProduct(ShipForward, ToOtherDir);
						
						// 상대방 배가 내 전방 30도 이내에 있고 (cos(30도) = 0.866)
						if (ForwardDot > 0.866f)
						{
							FVector MemberForward = Member->GetActorForwardVector();
							MemberForward.Z = 0.f;
							MemberForward.Normalize();

							float OtherForwardDot = FVector::DotProduct(MemberForward, -ToOtherDir);
							
							// 상대방 배도 나를 향해 정면 45도 이내로 다가오고 있을 때 (마주 보고 달리는 상태)
							if (OtherForwardDot > 0.707f)
							{
								FVector ShipRight = MyShip->GetActorRightVector();
								ShipRight.Z = 0.f;
								ShipRight.Normalize();

								// 내 기준 왼쪽(Port) 레이들에 가상 위험도 가산 적용 -> 우현 회피 유도
								for (int32 i = 0; i < NumRays; ++i)
								{
									float RayRightDot = FVector::DotProduct(Rays[i], ShipRight);
									if (RayRightDot < -0.2f) // 내 좌향 레이들
									{
										Danger[i] = FMath::Max(Danger[i], 0.35f * DangerWeight);
									}
								}
							}
						}
					}
				}
			}

			// D. 최종 점수(Interest - Danger) 최댓값 레이 선택
			float BestScore = -999.f;
			int32 BestRayIndex = -1;
			for (int32 i = 0; i < NumRays; ++i)
			{
				float Score = Interest[i] - Danger[i];
				if (Score > BestScore)
				{
					BestScore = Score;
					BestRayIndex = i;
				}
			}

			// 원래 가려던 방향(BaseHeading)과 가장 일치하는 Interest 최대 레이 인덱스 탐색
			int32 MaxInterestIndex = -1;
			float MaxInterestVal = -999.f;
			for (int32 i = 0; i < NumRays; ++i)
			{
				if (Interest[i] > MaxInterestVal)
				{
					MaxInterestVal = Interest[i];
					MaxInterestIndex = i;
				}
			}

			if (BestRayIndex != -1)
			{
				CachedDesiredHeading = Rays[BestRayIndex];
				
				// 회피 상태 판정: 선택된 방향이 원래 가려던 최적 관심도 방향과 다르거나, 원래 방향에 장벽(Danger)이 존재할 때
				if (MaxInterestIndex != -1)
				{
					bIsAvoiding = (BestRayIndex != MaxInterestIndex) || (Danger[MaxInterestIndex] > 0.01f);
				}
				else
				{
					bIsAvoiding = (Danger[BestRayIndex] > 0.01f);
				}
			}
			else
			{
				CachedDesiredHeading = BaseHeading;
				bIsAvoiding = false;
			}
		}
		DesiredHeading = CachedDesiredHeading;
	}
	else
	{
		CachedDesiredHeading = BaseHeading;
		DesiredHeading = BaseHeading;
		bIsAvoiding = false;
	}

	// --- 4.6. 회피 상태 로그 피드백 반영 ---
	if (bIsAvoiding)
	{
		StateString += TEXT(" (Avoiding)");
		StateColor = FColor::Magenta;
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
	// Feed AI intent into the same async Network Physics path as player input.
	// Server-authored inputs are recorded and sent to simulated proxies for prediction/resimulation.
	const float NetworkMoveInput = FMath::Clamp(
		MoveInput * FMath::Max(0.0f, ForwardForceMultiplier), -1.0f, 1.0f);
	const float NetworkTurnInput = FMath::Clamp(
		TurnInput * FMath::Max(0.0f, TurnTorqueMultiplier), -1.0f, 1.0f);
	MyShip->SetAIControlInput(NetworkMoveInput, NetworkTurnInput);

	/* Enemy Ship Network Physics input/state diagnostic log disabled after validation.
	if (StateAtTickStart != CurrentState)
	{
		UE_LOG(LogTemp, Log, TEXT("[NAVAL-AI-STATE] Ship=%s State=%s Target=%s Distance=%.1f Move=%.3f Turn=%.3f"),
			*MyShip->GetName(),
			*StaticEnum<ENavalCombatState>()->GetNameStringByValue(static_cast<int64>(CurrentState)),
			*GetNameSafe(TargetShip),
			Distance,
			NetworkMoveInput,
			NetworkTurnInput);
	}
	*/

	// --- 7. 화면 좌상단 스크린 디버그 로그 표시 ---
	const bool bNavalDebugEnabled = CVarShowNavalAIDebug.GetValueOnGameThread() > 0;
	if (bNavalDebugEnabled && GEngine)
	{
		FString SquadStr = CurrentSquadID.IsNone() ? TEXT("NoSquad") : CurrentSquadID.ToString();
		FString DebugMsg = FString::Printf(TEXT("Naval Drive: %s [%s] -> %s"), 
			*MyShip->GetName(), *SquadStr, *StateString);
		
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(MyShip->GetUniqueID()), 1.0f, StateColor, DebugMsg);
	}

	// --- 8. 디버그 원 시각화 (Z축 오프셋 적용) ---
	if (bNavalDebugEnabled && bShowDebugRanges)
	{
		/*
		UWorld* World = MyShip->GetWorld();
		if (World)
		{
			FVector CenterLoc = SelfLoc;
			CenterLoc.Z += DebugZOffset; // 바다 표면 위로 오프셋 적용

			// IdealDistance (선회 대치선 - 초록색 원)
			DrawDebugCircle(
				World,
				CenterLoc,
				CurrentIdealDistance,
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
				CurrentIdealDistance + OrbitTolerance,
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

			// 컨텍스트 스티어링 최종 조타 방향 시각화 (보라색 화살표)
			if (TargetShip && CurrentState != ENavalCombatState::Idle && CurrentState != ENavalCombatState::Return)
			{
				DrawDebugDirectionalArrow(
					World,
					CenterLoc,
					CenterLoc + DesiredHeading * 800.f,
					150.f,
					FColor::Magenta,
					false,
					-1.f,
					0,
					5.f
				);
			}

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
		*/
	}

	// --- 9. AEnemyShip 전용 대포 AI 정보 동기화 ---
	if (AEnemyShip* EnemyShip = Cast<AEnemyShip>(MyShip))
	{
		EnemyShip->SetAITarget(TargetShip);
		EnemyShip->SetNavalCombatState(CurrentState);
		EnemyShip->SetMaxActiveCannons(MaxActiveCannons);
	}
}

AShip* UBTTask_NavalDrive::FindPlayerShip(UWorld* World, APawn* AIPawn) const
{
	if (!World || !AIPawn) return nullptr;

	AShip* ClosestPlayerShip = nullptr;
	float ClosestDistance = FLT_MAX;
	FVector SelfLoc = AIPawn->GetActorLocation();

	// Possess 여부와 무관하게 월드의 플레이어 배를 탐색한다.
	for (TActorIterator<AShip> Iterator(World); Iterator; ++Iterator)
	{
		AShip* FoundShip = *Iterator;
		if (!FoundShip || FoundShip == AIPawn) continue;
		if (!FoundShip->ActorHasTag(TEXT("Player")) || FoundShip->ActorHasTag(TEXT("Enemy"))) continue;

		// 가장 가까운 위치의 플레이어 배를 최종 타겟팅 대상으로 선택
		const float Dist = FVector::Dist(SelfLoc, FoundShip->GetActorLocation());
		if (Dist < ClosestDistance)
		{
			ClosestDistance = Dist;
			ClosestPlayerShip = FoundShip;
		}
	}

	return ClosestPlayerShip;
}
