#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "StoryTypes.h"
#include "StorySaveGame.generated.h"

UCLASS()
class STORY_API UStorySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 1;

	UPROPERTY(SaveGame)
	FPrimaryAssetId StoryDefinitionId;

	UPROPERTY(SaveGame)
	FGameplayTagContainer Facts;

	UPROPERTY(SaveGame)
	TArray<FStoryCounterValue> Counters;

	UPROPERTY(SaveGame)
	TArray<FName> AppliedActionKeys;
};
