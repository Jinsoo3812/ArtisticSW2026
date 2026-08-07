#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "StoryTypes.generated.h"

UENUM(BlueprintType)
enum class EStoryCounterComparison : uint8
{
	Equal,
	NotEqual,
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual
};

USTRUCT(BlueprintType)
struct STORY_API FStoryCounterValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Story")
	FGameplayTag CounterTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Story")
	int32 Value = 0;
};

USTRUCT(BlueprintType)
struct STORY_API FStoryCounterRequirement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTag CounterTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	EStoryCounterComparison Comparison = EStoryCounterComparison::GreaterOrEqual;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	int32 Value = 0;

	bool IsSatisfied(const TMap<FGameplayTag, int32>& Counters) const;
};

/**
 * A data-only query over the shared campaign state.
 *
 * FactQuery supports nested ALL/ANY/NOT expressions through FGameplayTagQuery.
 * CounterRequirements are ANDed with the fact query.
 */
USTRUCT(BlueprintType)
struct STORY_API FStoryConditionSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTagQuery FactQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FStoryCounterRequirement> CounterRequirements;

	bool IsSatisfied(
		const FGameplayTagContainer& Facts,
		const TMap<FGameplayTag, int32>& Counters) const;

	bool IsEmpty() const
	{
		return FactQuery.IsEmpty() && CounterRequirements.IsEmpty();
	}
};

/**
 * Opaque command emitted once when a story state is activated.
 *
 * Story intentionally does not know the concrete receiver type. Enemy,
 * ClassFeature, WaterAndShip, or Blueprint receivers interpret the payload.
 */
USTRUCT(BlueprintType)
struct STORY_API FStoryActionSpec
{
	GENERATED_BODY()

	/** Stable within its owning state. Changing it intentionally re-applies the action. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FName ActionId = NAME_None;

	/** Routing key, for example Story.Action.Skill.Unlock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTag ActionType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTag TargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FName TargetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	int32 IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	float FloatValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TSoftObjectPtr<UObject> Asset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTagContainer ContextTags;
};

/**
 * One additive campaign milestone.
 *
 * States do not form a single enum. Several states may be active at once,
 * allowing optional objectives and future story branches.
 */
USTRUCT(BlueprintType)
struct STORY_API FStoryStateRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTag StateTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FStoryConditionSet ActivationCondition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTagContainer GrantedFacts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FGameplayTagContainer RemovedFacts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FStoryActionSpec> ActivationActions;
};

USTRUCT(BlueprintType)
struct STORY_API FStoryProgressSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Story")
	FGameplayTagContainer Facts;

	UPROPERTY(BlueprintReadOnly, Category = "Story")
	TArray<FStoryCounterValue> Counters;

	UPROPERTY(BlueprintReadOnly, Category = "Story")
	int32 Revision = 0;
};
