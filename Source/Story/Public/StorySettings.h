#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StorySettings.generated.h"

class UStoryDefinition;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Story"))
class STORY_API UStorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category = "Startup")
	TSoftObjectPtr<UStoryDefinition> DefaultStoryDefinition;

	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	FString DefaultSaveSlot = TEXT("StoryCampaign");

	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	bool bAutoLoadDefaultSlot = false;
};
