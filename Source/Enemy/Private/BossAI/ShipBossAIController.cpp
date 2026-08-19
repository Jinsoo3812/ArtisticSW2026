#include "BossAI/ShipBossAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipBossAI, Log, All);

void AShipBossAIController::OnPossess(APawn* PossessedPawn)
{
	Super::OnPossess(PossessedPawn);

	if (!HasAuthority())
	{
		return;
	}

	AShipBossEnemy* Boss = Cast<AShipBossEnemy>(PossessedPawn);
	if (!Boss)
	{
		UE_LOG(LogShipBossAI, Error,
			TEXT("ShipBossAIController can only possess ShipBossEnemy. Pawn=%s"),
			*GetNameSafe(PossessedPawn));
		return;
	}

	UBehaviorTree* BehaviorTree = Boss->GetBehaviorTree();
	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	UBehaviorTreeComponent* BehaviorTreeComponent = Cast<UBehaviorTreeComponent>(GetBrainComponent());
	if (!BehaviorTree)
	{
		UE_LOG(LogShipBossAI, Error,
			TEXT("Boss has no Behavior Tree. BT-only AI will remain stopped. Boss=%s"),
			*GetNameSafe(Boss));
		return;
	}
	if (!BehaviorTree->BlackboardAsset)
	{
		UE_LOG(LogShipBossAI, Error,
			TEXT("Boss Behavior Tree has no Blackboard. Boss=%s Tree=%s"),
			*GetNameSafe(Boss), *GetNameSafe(BehaviorTree));
		return;
	}
	if (!BlackboardComponent || BlackboardComponent->GetBlackboardAsset() != BehaviorTree->BlackboardAsset)
	{
		UE_LOG(LogShipBossAI, Error,
			TEXT("Boss Blackboard initialization or asset match failed. Boss=%s Tree=%s ExpectedBB=%s ActualBB=%s"),
			*GetNameSafe(Boss),
			*GetNameSafe(BehaviorTree),
			*GetNameSafe(BehaviorTree->BlackboardAsset),
			*GetNameSafe(BlackboardComponent ? BlackboardComponent->GetBlackboardAsset() : nullptr));
		return;
	}
	if (!BehaviorTreeComponent || !BehaviorTreeComponent->IsRunning())
	{
		UE_LOG(LogShipBossAI, Error,
			TEXT("Boss Behavior Tree brain did not start. Boss=%s Tree=%s Brain=%s"),
			*GetNameSafe(Boss), *GetNameSafe(BehaviorTree), *GetNameSafe(GetBrainComponent()));
		return;
	}

	static const FName DestinationPointKeyName(TEXT("DestinationPointId"));
	if (BlackboardComponent->GetKeyID(DestinationPointKeyName) != FBlackboard::InvalidKey)
	{
		BlackboardComponent->SetValueAsInt(DestinationPointKeyName, INDEX_NONE);
	}
	else
	{
		UE_LOG(LogShipBossAI, Error,
			TEXT("Boss Blackboard is missing required Int key DestinationPointId. Blackboard=%s"),
			*GetNameSafe(BehaviorTree->BlackboardAsset));
	}

	// InitializeBoss can run before possession in deferred or pooled spawn flows.
	// Restore that target after the shared controller initialized the Blackboard.
	if (!GetCombatTarget())
	{
		if (AActor* InitialTarget = Boss->GetBossCombatTarget())
		{
			SetCombatTarget(InitialTarget);
		}
	}

	UE_LOG(LogShipBossAI, Log,
		TEXT("Boss BT-only AI started. Boss=%s Tree=%s Blackboard=%s Target=%s"),
		*GetNameSafe(Boss),
		*GetNameSafe(BehaviorTree),
		*GetNameSafe(BehaviorTree->BlackboardAsset),
		*GetNameSafe(GetCombatTarget()));
}
