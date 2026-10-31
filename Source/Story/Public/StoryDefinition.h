#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StoryTypes.h"
#include "StoryDefinition.generated.h"

/**
 * Legacy asset schema retained for existing asset references only.
 * Runtime campaign progression uses StoryFacadeSubsystem; these rules are not loaded or executed.
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
