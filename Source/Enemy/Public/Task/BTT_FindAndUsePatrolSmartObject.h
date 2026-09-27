#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "SmartObjectTypes.h"
#include "BTT_FindAndUsePatrolSmartObject.generated.h"

class UAITask_UseGameplayBehaviorSmartObject;

struct FBTUsePatrolSmartObjectMemory
{
	TWeakObjectPtr<UAITask_UseGameplayBehaviorSmartObject> TaskInstance;
};

/** Finds, claims, reaches, and executes a passive Smart Object without EQS. */
UCLASS()
class ENEMY_API UBTT_FindAndUsePatrolSmartObject : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_FindAndUsePatrolSmartObject();

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual void InitializeMemory(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTMemoryClear::Type CleanupType) const override;
	virtual uint16 GetInstanceMemorySize() const override
	{
		return sizeof(FBTUsePatrolSmartObjectMemory);
	}
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blackboard")
	FBlackboardKeySelector HomeLocationKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blackboard")
	FBlackboardKeySelector PatrolRadiusKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smart Object")
	FGameplayTagQuery ActivityRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smart Object")
	ESmartObjectClaimPriority ClaimPriority = ESmartObjectClaimPriority::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smart Object", meta = (ClampMin = "0.0", Units = "cm"))
	float FallbackRadius = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smart Object", meta = (ClampMin = "0.0", Units = "cm"))
	float VerticalTolerance = 300.0f;
};
