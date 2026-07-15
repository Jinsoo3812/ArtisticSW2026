// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BaseAIController.h"

// Player Folder
#include "BasePlayer.h"

// Enemy Folder
#include "BaseEnemy.h"

// Unreal Folder
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISenseConfig.h"
#include "Perception/AISenseConfig_Sight.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/BaseHealthComponent.h"

namespace
{
	bool IsDeadActor(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		const UBaseHealthComponent* HealthComponent = Actor->FindComponentByClass<UBaseHealthComponent>();
		return HealthComponent && HealthComponent->IsDead();
	}
}


// Sets default values
ABaseAIController::ABaseAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	SetupPerceptionSystem();
}

void ABaseAIController::SetupPerceptionSystem()
{
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	if (SightConfig)
	{
		SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent")));

		// AI Sight가 Target을 감지하기 시작하는 범위
		SightConfig->SightRadius = 1000.f;
		// AI Sight가 Target을 놓치기 시작하는 범위
		SightConfig->LoseSightRadius = SightConfig->SightRadius + 100.f;
		// AI Sight가 Target을 감지하고 유지하는 시간
		SightConfig->SetMaxAge(5.f);
		// AI Sight의 시야각
		SightConfig->PeripheralVisionAngleDegrees = 65.f;
		// AI 시야 범위에서 벗어났더라도 마지막으로 본 위치에서 일정 범위 내에 있으면 자동으로 성공하도록 하는 범위 설정
		SightConfig->AutoSuccessRangeFromLastSeenLocation = 500.f;
		// AI Sight가 감지할 수 있는 Target의 종류 설정 (적, 중립, 아군 모두 감지하도록 설정)
		SightConfig->DetectionByAffiliation.bDetectEnemies = true;
		SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
		SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

		
		GetPerceptionComponent()->SetDominantSense(*SightConfig->GetSenseImplementation());
		GetPerceptionComponent()->OnTargetPerceptionUpdated.AddDynamic(this, &ABaseAIController::OnTargetSighted);
		GetPerceptionComponent()->ConfigureSense(*SightConfig);
	}
	
}

void ABaseAIController::OnPossess(APawn* PossessedPawn)
{
	Super::OnPossess(PossessedPawn);

	// Run Behavior Tree
	if (ABaseEnemy* PossessedEnemy = Cast<ABaseEnemy>(PossessedPawn))
	{
		if (PossessedEnemy->GetBehaviorTree())
		{
			UBlackboardComponent* Bboard;
			UseBlackboard(PossessedEnemy->GetBehaviorTree()->BlackboardAsset, Bboard);
			Blackboard = Bboard;

			InitializeBlackboardValues(PossessedPawn);
			
			RunBehaviorTree(PossessedEnemy->GetBehaviorTree());
		}
	}
	
}

void ABaseAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindPerceivedTargetDeath();

	Super::EndPlay(EndPlayReason);
}

void ABaseAIController::OnTargetSighted(AActor* SeenTarget, FAIStimulus Stimulus)
{
	// 관찰된 Player를 Blackboard에 넣어서 Behavior Tree에서 사용할 수 있도록 하는 로직을 작성할 수 있습니다.
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!BlackboardComponent)
	{
		return;
	}

	if (ABasePlayer* PlayerTarget = Cast<ABasePlayer>(SeenTarget))
	{
		if (Stimulus.WasSuccessfullySensed())
		{
			if (IsDeadActor(PlayerTarget))
			{
				BlackboardComponent->ClearValue(TargetActorKeyName);
				UnbindPerceivedTargetDeath();
				return;
			}

			// 시야에 들어옴
			BlackboardComponent->SetValueAsObject(TargetActorKeyName, PlayerTarget);
			BindPerceivedTargetDeath(PlayerTarget);
		}
		else
		{
			// 시야에서 벗어남 (필요에 따라 처리)
			BlackboardComponent->ClearValue(TargetActorKeyName);
			UnbindPerceivedTargetDeath();
		}
	}
}

void ABaseAIController::OnPerceivedTargetDeathStarted(UBaseHealthComponent* HealthComponent)
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (BlackboardComponent)
	{
		BlackboardComponent->ClearValue(TargetActorKeyName);
	}

	UnbindPerceivedTargetDeath();
}

void ABaseAIController::InitializeBlackboardValues(APawn* PossessedPawn)
{
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	if (!BlackboardComponent || !PossessedPawn)
	{
		return;
	}

	BlackboardComponent->ClearValue(TargetActorKeyName);
	BlackboardComponent->SetValueAsVector(HomeLocationKeyName, PossessedPawn->GetActorLocation());
	BlackboardComponent->SetValueAsFloat(PatrolRadiusKeyName, DefaultPatrolRadius);
}

void ABaseAIController::BindPerceivedTargetDeath(AActor* TargetActor)
{
	UBaseHealthComponent* HealthComponent = TargetActor ? TargetActor->FindComponentByClass<UBaseHealthComponent>() : nullptr;
	if (ObservedTargetHealthComponent.Get() == HealthComponent)
	{
		return;
	}

	UnbindPerceivedTargetDeath();

	if (!HealthComponent)
	{
		return;
	}

	ObservedTargetHealthComponent = HealthComponent;
	HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &ABaseAIController::OnPerceivedTargetDeathStarted);
}

void ABaseAIController::UnbindPerceivedTargetDeath()
{
	if (UBaseHealthComponent* HealthComponent = ObservedTargetHealthComponent.Get())
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &ABaseAIController::OnPerceivedTargetDeathStarted);
	}

	ObservedTargetHealthComponent.Reset();
}
