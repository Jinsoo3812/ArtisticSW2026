// Fill out your copyright notice in the Description page of Project Settings.


#include "ShipAI/NavalAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"

ANavalAIController::ANavalAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// AIPerception 컴포넌트 생성
	PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	if (SightConfig && PerceptionComp)
	{
		SetPerceptionComponent(*PerceptionComp);

		// 시각 설정
		SightConfig->SightRadius = 4000.f;
		SightConfig->LoseSightRadius = 4500.f;
		SightConfig->PeripheralVisionAngleDegrees = 180.f; // 180도 광시야각 감지
		SightConfig->SetMaxAge(5.f);
		SightConfig->DetectionByAffiliation.bDetectEnemies = true;
		SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
		SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

		PerceptionComp->ConfigureSense(*SightConfig);
		PerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());
	}
}

void ANavalAIController::BeginPlay()
{
	Super::BeginPlay();

	if (PerceptionComp)
	{
		PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ANavalAIController::OnTargetPerceptionUpdated);
	}
}

void ANavalAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 비헤이비어 트리 실행
	if (DefaultBehaviorTree)
	{
		UBlackboardComponent* Bboard = nullptr;
		if (UseBlackboard(DefaultBehaviorTree->BlackboardAsset, Bboard))
		{
			Blackboard = Bboard;
			RunBehaviorTree(DefaultBehaviorTree);
		}
	}
}

void ANavalAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Blackboard) return;

	// 감지된 액터가 배(AShip)인지 검사
	if (AShip* TargetShip = Cast<AShip>(Actor))
	{
		if (Stimulus.WasSuccessfullySensed())
		{
			// 플레이어가 탑승하고 있는 상태인지 체크하려면 TargetShip->GetController()나
			// 리플리케이트되는 탑승자 변수가 유효한지 검사할 수 있습니다.
			// 여기서는 감지된 AShip을 바로 TargetShip 블랙보드 키에 저장합니다.
			Blackboard->SetValueAsObject(TEXT("TargetShip"), TargetShip);
		}
		else
		{
			// 시야에서 사라진 경우 블랙보드 키 클리어
			// (대상이 자신이 설정한 TargetShip인지 한 번 더 확인)
			UObject* CurrentTarget = Blackboard->GetValueAsObject(TEXT("TargetShip"));
			if (CurrentTarget == TargetShip)
			{
				Blackboard->ClearValue(TEXT("TargetShip"));
			}
		}
	}
}
