#pragma once
#include "CoreMinimal.h"
#include "Task/BTT_MoveToDeckWaypoint.h"
#include "BossAI/BossDeckPointSelector.h"
#include "BTT_RepositionSamuraiAfterDashFailure.generated.h"

/** Selects a fresh Walk goal, then reuses deck movement and its timeout/abort cleanup. */
UCLASS()
class ENEMY_API UBTT_RepositionSamuraiAfterDashFailure : public UBTT_MoveToDeckWaypoint
{
	GENERATED_BODY()
public:
	UBTT_RepositionSamuraiAfterDashFailure();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
	virtual FString GetStaticDescription() const override;
protected:
	UPROPERTY(EditAnywhere, Category = "Samurai|Reposition")
	FBossDestinationSelectionSettings WalkSelectionSettings;
private:
	bool bStartedReposition = false;
	TWeakObjectPtr<AActor> RepositionTarget;
};
