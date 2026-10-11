#pragma once
#include "CoreMinimal.h"
#include "Task/BTT_SelectBossDestinationPoint.h"
#include "BTT_SelectSamuraiDashPoint.generated.h"

/** Bounded retries for Samurai Dash only; shared Walk/Vanish selector behavior is unchanged. */
UCLASS()
class ENEMY_API UBTT_SelectSamuraiDashPoint : public UBTT_SelectBossDestinationPoint
{
	GENERATED_BODY()
public:
	UBTT_SelectSamuraiDashPoint();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
	virtual FString GetStaticDescription() const override;
private:
	EBTNodeResult::Type TrySelect(UBehaviorTreeComponent& OwnerComp);
};
