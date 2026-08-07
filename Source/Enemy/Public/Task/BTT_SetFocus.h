#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"

#include "BTT_SetFocus.generated.h"

/** Sets the AI controller's Gameplay focus to the selected Blackboard actor. */
UCLASS()
class ENEMY_API UBTT_SetFocus : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_SetFocus();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
