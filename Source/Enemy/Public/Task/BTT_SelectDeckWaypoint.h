#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_SelectDeckWaypoint.generated.h"

UENUM(BlueprintType)
enum class EDeckWaypointSelectionMode : uint8
{
	Patrol,
	Combat,
	ReleaseLineOfSightReposition
};

/** Selects patrol, routed combat, or release-LOS recovery movement. */
UCLASS()
class ENEMY_API UBTT_SelectDeckWaypoint : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_SelectDeckWaypoint();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
	EDeckWaypointSelectionMode GetSelectionMode() const { return SelectionMode; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI")
	EDeckWaypointSelectionMode SelectionMode = EDeckWaypointSelectionMode::Patrol;
};
