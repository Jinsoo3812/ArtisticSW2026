// Fill out your copyright notice in the Description page of Project Settings.


#include "ShipAI/NavalAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Ship.h"
#include "EngineUtils.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

ANavalAIController::ANavalAIController()
{
	PrimaryActorTick.bCanEverTick = true;

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

void ANavalAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}

	TargetRefreshRemaining -= DeltaSeconds;
	if (TargetRefreshRemaining <= 0.0f)
	{
		TargetRefreshRemaining = FMath::Max(0.05f, TargetRefreshInterval);
		RefreshTargetShip();
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
	TargetRefreshRemaining = 0.0f;
	RefreshTargetShip();
}

void ANavalAIController::OnUnPossess()
{
	SetTargetShip(nullptr);
	Super::OnUnPossess();
}

void ANavalAIController::SetTargetShip(AShip* InTargetShip)
{
	AEnemyShip* EnemyShip = Cast<AEnemyShip>(GetPawn());
	if (InTargetShip == EnemyShip || (InTargetShip && InTargetShip->IsEnemyShipForEffects()))
	{
		return;
	}

	TargetShip = InTargetShip;
	if (UBlackboardComponent* BlackboardComponent = GetBlackboardComponent())
	{
		if (InTargetShip)
		{
			BlackboardComponent->SetValueAsObject(TargetShipKeyName, InTargetShip);
		}
		else
		{
			BlackboardComponent->ClearValue(TargetShipKeyName);
		}
	}
	if (UEnemyShipNavigationComponent* Navigation = EnemyShip ? EnemyShip->GetNavigationComponent() : nullptr)
	{
		Navigation->SetTargetShip(InTargetShip);
	}
}

AShip* ANavalAIController::FindClosestPlayerShip() const
{
	const AEnemyShip* EnemyShip = Cast<AEnemyShip>(GetPawn());
	UWorld* World = EnemyShip ? EnemyShip->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	AShip* Closest = nullptr;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<AShip> It(World); It; ++It)
	{
		AShip* Candidate = *It;
		if (!IsValid(Candidate) || Candidate == EnemyShip
			|| Candidate->IsEnemyShipForEffects()
			|| !Candidate->ActorHasTag(TEXT("Player"))
			|| Candidate->ActorHasTag(TEXT("Enemy")))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(
			EnemyShip->GetActorLocation(),
			Candidate->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			Closest = Candidate;
		}
	}
	return Closest;
}

void ANavalAIController::RefreshTargetShip()
{
	AEnemyShip* EnemyShip = Cast<AEnemyShip>(GetPawn());
	UEnemyShipNavigationComponent* Navigation = EnemyShip ? EnemyShip->GetNavigationComponent() : nullptr;
	if (!EnemyShip || !Navigation)
	{
		SetTargetShip(nullptr);
		return;
	}

	AShip* Candidate = FindClosestPlayerShip();
	if (Candidate)
	{
		const float Distance = FVector::Dist2D(EnemyShip->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance > Navigation->GetNavigationProfile().DetectionDistance)
		{
			Candidate = nullptr;
		}
	}
	SetTargetShip(Candidate);
}
