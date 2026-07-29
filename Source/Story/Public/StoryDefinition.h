#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StoryTypes.h"
#include "StoryDefinition.generated.h"

/**
 * Static campaign rules. Runtime progress never belongs in this asset.
 */
UCLASS(BlueprintType, Const)
class STORY_API UStoryDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Story")
	int32 DataVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Story")
	FGameplayTagContainer InitialFacts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Story")
	TArray<FStoryCounterValue> InitialCounters;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Story")
	TArray<FStoryStateRule> StateRules;

	const FStoryStateRule* FindStateRule(FGameplayTag StateTag) const;

	UFUNCTION(BlueprintCallable, Category = "Story|Validation")
	bool ValidateDefinition(TArray<FText>& OutErrors) const;
};
