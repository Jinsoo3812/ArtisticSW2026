// Fill out your copyright notice in the Description page of Project Settings.


#include "ShipAI/NavalAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"

ANavalAIController::ANavalAIController()
{
	PrimaryActorTick.bCanEverTick = false;

	// AIPerception 컴포넌트 생성 (기본 활성화 유지)
	PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	if (SightConfig && PerceptionComp)
	{
		SetPerceptionComponent(*PerceptionComp);

		SightConfig->SightRadius = 10000.f;
		SightConfig->LoseSightRadius = 11000.f;
		SightConfig->PeripheralVisionAngleDegrees = 180.f;
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
