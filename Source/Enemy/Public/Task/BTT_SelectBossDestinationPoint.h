#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BossAI/BossDeckPointSelector.h"
#include "BTT_SelectBossDestinationPoint.generated.h"

UCLASS()
class ENEMY_API UBTT_SelectBossDestinationPoint : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_SelectBossDestinationPoint();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** Output integer key. World positions are intentionally not cached on a moving ship. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	EBossDestinationPurpose SelectionPurpose = EBossDestinationPurpose::Vanish;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	FBossDestinationSelectionSettings SelectionSettings;
};
