#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_SelectDeckWaypoint.generated.h"

UENUM(BlueprintType)
enum class EDeckWaypointSelectionMode : uint8
{
	Patrol,
	Combat
};

/** Selects only a directly linked point; no NavMesh or full path search is used in the MVP. */
UCLASS()
class ENEMY_API UBTT_SelectDeckWaypoint : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_SelectDeckWaypoint();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI")
	EDeckWaypointSelectionMode SelectionMode = EDeckWaypointSelectionMode::Patrol;
};
