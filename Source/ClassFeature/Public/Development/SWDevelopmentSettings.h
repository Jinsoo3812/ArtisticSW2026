#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "SWDevelopmentSettings.generated.h"

/** One item type and its desired inventory count when starting an editor test. */
USTRUCT(BlueprintType)
struct FStartingInventoryItemForTest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Testing|Inventory",
		meta = (Categories = "Item.Id"))
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Testing|Inventory",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 Count = 1;
};


/** Editor start inventory, separated from every player Blueprint. Runtime cheats use SW.DevTest commands. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="SW Development Tests"))
class CLASSFEATURE_API USWDevelopmentSettings : public UDeveloperSettings
{
 GENERATED_BODY()
public:
 virtual FName GetCategoryName() const override { return TEXT("Game"); }
 /** Applies on authority to fresh inventories in editor and packaged games. Restored inventories are preserved. */
 UPROPERTY(Config, EditAnywhere, Category="Starting Inventory")
 bool bGiveStartingItems = false;
 UPROPERTY(Config, EditAnywhere, Category="Starting Inventory", meta=(EditCondition="bGiveStartingItems", TitleProperty="ItemTag"))
 TArray<FStartingInventoryItemForTest> StartingItems;
};
