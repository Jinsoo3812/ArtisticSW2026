#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "EnemyAITypes.generated.h"

class UBehaviorTree;

/**
 * High-level intent used to route an Enemy through the shared Behavior Tree.
 *
 * Explicit values are intentional: Blackboard enum values are serialized in assets.
 * Add new values without changing or reusing an existing numeric value.
 */
UENUM(BlueprintType)
enum class EEnemyAIState : uint8
{
	Passive = 0 UMETA(DisplayName = "Passive"),
	Investigating = 10 UMETA(DisplayName = "Investigating"),
	Combat = 20 UMETA(DisplayName = "Combat"),
	Frozen = 30 UMETA(DisplayName = "Frozen"),
	Dead = 250 UMETA(DisplayName = "Dead")
};

/** One Run Behavior Dynamic injection configured for an Enemy archetype. */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyStateBehavior
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	EEnemyAIState State = EEnemyAIState::Passive;

	/** Must match the Injection Tag on a Run Behavior Dynamic node in the root tree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (Categories = "AI.Behavior"))
	FGameplayTag InjectionTag;

	/** Subtree that contains behavior for this state only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UBehaviorTree> Subtree = nullptr;
};

