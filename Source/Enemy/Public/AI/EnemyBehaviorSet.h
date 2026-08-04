#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AI/EnemyAITypes.h"

#include "EnemyBehaviorSet.generated.h"

/**
 * Data-driven mapping from shared AI states to Enemy-specific dynamic subtrees.
 * Multiple Enemy Blueprints can reuse one set, while a specialized Enemy can
 * replace only the state subtrees that differ from the default archetype.
 */
UCLASS(BlueprintType)
class ENEMY_API UEnemyBehaviorSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	UBehaviorTree* FindSubtree(EEnemyAIState State) const;

	const TArray<FEnemyStateBehavior>& GetStateBehaviors() const { return StateBehaviors; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI", meta = (TitleProperty = "State"))
	TArray<FEnemyStateBehavior> StateBehaviors;
};

