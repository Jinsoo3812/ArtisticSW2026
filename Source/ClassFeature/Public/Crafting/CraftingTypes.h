#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CraftingTypes.generated.h"

class AActor;
class UTexture2D;

UENUM(BlueprintType)
enum class ECraftingAvailability : uint8
{
	Available,
	MissingRecipe,
	MissingIngredients,
	OutputUnavailable,
	Disabled
};

UENUM(BlueprintType)
enum class ECraftingOutputType : uint8
{
	Inventory,
	ExternalReceiver
};

UENUM(BlueprintType)
enum class ECraftingFailureReason : uint8
{
	Success,
	InvalidRecipe,
	RecipeDisabled,
	MissingRecipe,
	NoActiveContext,
	OutOfRange,
	InvalidQuantity,
	MissingIngredients,
	OutputUnavailable,
	OutputRejected,
	DuplicateRequest,
	InternalError
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingListQuery
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	FString SearchText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	bool bIncludeLocked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	bool bIncludeDisabled = false;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingListEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FName RecipeId;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGameplayTag ResultItemTag;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGameplayTag CategoryTag;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGameplayTag RarityTag;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 ResultQuantity = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	ECraftingAvailability Availability = ECraftingAvailability::Disabled;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingIngredientView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGameplayTag ItemTag;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 OwnedQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 RequiredQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	bool bEnough = false;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingDetailsView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FCraftingListEntry Header;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGameplayTag RequiredRecipeItemTag;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	bool bIngredientsVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	TArray<FCraftingIngredientView> Ingredients;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 MaxCraftableCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	ECraftingAvailability Availability = ECraftingAvailability::Disabled;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingOutputRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	ECraftingOutputType Type = ECraftingOutputType::Inventory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	TObjectPtr<AActor> ReceiverActor;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	FName RecipeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (ClampMin = "1", ClampMax = "99"))
	int32 CraftCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	FCraftingOutputRequest Output;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FCraftingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FName RecipeId;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FGameplayTag ResultItemTag;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 DeliveredQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	ECraftingFailureReason Reason = ECraftingFailureReason::InternalError;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCraftingScreenOpened, AActor*, ContextActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCraftingScreenClosed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCraftingResult, const FCraftingResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCraftingDataChanged);
