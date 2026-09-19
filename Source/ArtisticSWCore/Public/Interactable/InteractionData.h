#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InteractionData.generated.h"

/** Data-driven presentation shared by non-item interactable types. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FInteractionFeatureData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|UI")
	FText ObjectName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|UI")
	FText ActionText;
};
