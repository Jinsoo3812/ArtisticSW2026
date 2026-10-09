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
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(float); }
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;
	void SetSelectionPurpose(EBossDestinationPurpose InPurpose) { SelectionPurpose = InPurpose; }
	EBossDestinationPurpose GetSelectionPurpose() const { return SelectionPurpose; }
	void SetDestinationRelation(EBossDestinationRelation InRelation) { DestinationRelation = InRelation; }
	EBossDestinationRelation GetDestinationRelation() const { return DestinationRelation; }
	const FBossDestinationSelectionSettings& GetSelectionSettings() const { return SelectionSettings; }

protected:
	/** Combat target; destination output uses BlackboardKey's ship-local vector. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	EBossDestinationPurpose SelectionPurpose = EBossDestinationPurpose::Vanish;

	/** Existing serialized tasks keep the original rear-placement behavior by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	EBossDestinationRelation DestinationRelation = EBossDestinationRelation::BehindTarget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Point")
	FBossDestinationSelectionSettings SelectionSettings;
};
