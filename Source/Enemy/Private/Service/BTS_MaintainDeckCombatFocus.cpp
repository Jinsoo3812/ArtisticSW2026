#include "Service/BTS_MaintainDeckCombatFocus.h"
#include "AIController.h"
#include "AI/BaseAIController.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "ShipAI/EnemyShip.h"

UBTS_MaintainDeckCombatFocus::UBTS_MaintainDeckCombatFocus()
{
	NodeName = TEXT("Maintain Deck Combat Focus"); bCreateNodeInstance = true;
	bNotifyBecomeRelevant = bNotifyCeaseRelevant = bNotifyTick = true;
	Interval = 0.1f; RandomDeviation = 0.0f;
}
void UBTS_MaintainDeckCombatFocus::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	if (Combat.IsValid()) Combat->ReleaseFocus();
	ADeckEnemy* Enemy = OwnerComp.GetAIOwner() ? Cast<ADeckEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	Combat = Enemy ? Enemy->GetDeckCombatComponent() : nullptr;
	if (Combat.IsValid()) Combat->AcquireFocus();
}
void UBTS_MaintainDeckCombatFocus::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (Combat.IsValid()) Combat->ReleaseFocus(); Combat.Reset();
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);
}
void UBTS_MaintainDeckCombatFocus::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	if (Combat.IsValid()) Combat->RefreshFocus();
	ADeckEnemy* Enemy = OwnerComp.GetAIOwner() ? Cast<ADeckEnemy>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	ABaseAIController* AI = Cast<ABaseAIController>(OwnerComp.GetAIOwner());
	if (Enemy && AI && Combat.IsValid() && !Combat->HasCommittedAttack()
		&& Combat->HasStoredRecovery() && !Combat->HasRecovery())
	{
		FDeckWalkLocation Snapshot;
		const UDeckWalkAreaComponent* Area = Enemy->GetDeckHostShip() ? Enemy->GetDeckHostShip()->GetDeckWalkAreaComponent() : nullptr;
		if (Area && Combat->GetRecoveryGoal(Snapshot))
		{
			const FVector Point = Area->ToWorld(Snapshot.LocalFloor);
			AI->ClearCombatTarget(true); AI->StartInvestigation(Point);
		}
		else Combat->ClearRecovery();
	}
}
