#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "ClusterGrenadeSelectionWidget.generated.h"

class UTexture2D;

/**
 * UI Widget for Cluster Grenade Sub-munition selection.
 * C++ acts as the base class for the UMG Blueprint.
 */
UCLASS()
class CLASSFEATURE_API UClusterGrenadeSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Update the UI with the currently selected sub-munition
	UFUNCTION(BlueprintImplementableEvent, Category = "ClusterGrenade")
	void UpdateSelection(FGameplayTag SelectedTag, const FText& ItemName, UTexture2D* ItemIcon, int32 CurrentCount);

	// Optional: Play a visual effect or animation when selection changes
	UFUNCTION(BlueprintImplementableEvent, Category = "ClusterGrenade")
	void OnSelectionChanged();
};
