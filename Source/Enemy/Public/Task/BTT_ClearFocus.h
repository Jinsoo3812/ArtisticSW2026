#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"

#include "BTT_ClearFocus.generated.h"

/** Clears the AI controller's Gameplay focus. */
UCLASS()
class ENEMY_API UBTT_ClearFocus : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_ClearFocus();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
