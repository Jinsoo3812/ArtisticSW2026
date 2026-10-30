#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "GameplayTagContainer.h"
#include "InventoryItemDragDropOperation.generated.h"

/** Drag payload used when an inventory item is dropped onto a quick slot. */
UCLASS()
class CLASSFEATURE_API UInventoryItemDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	FGameplayTag ItemTag;
};
