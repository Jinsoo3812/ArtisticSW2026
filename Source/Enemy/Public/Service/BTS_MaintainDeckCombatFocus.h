#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTS_MaintainDeckCombatFocus.generated.h"
class UDeckEnemyCombatComponent;

UCLASS()
class ENEMY_API UBTS_MaintainDeckCombatFocus : public UBTService
{
	GENERATED_BODY()
public:
	UBTS_MaintainDeckCombatFocus();
protected:
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
private:
	TWeakObjectPtr<UDeckEnemyCombatComponent> Combat;
};
