#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CraftingAccessComponent.generated.h"

/**
 * Marks an integrated facility actor as a valid crafting context.
 * The owning actor handles Interaction and opens the map/upgrade/crafting hub UI.
 * This component is consulted only after the user selects the crafting tab.
 */
UCLASS(ClassGroup = (Crafting), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API UCraftingAccessComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCraftingAccessComponent();

	UFUNCTION(BlueprintPure, Category = "Crafting")
	float GetUseDistance() const { return UseDistance; }

	bool IsExternalReceiverAllowed(const AActor* Receiver) const;

protected:
	/** Player must remain this close to the integrated facility while crafting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (ClampMin = "0"))
	float UseDistance = 300.0f;

	/** Optional server-side allowlist for non-inventory crafting outputs. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Crafting")
	TArray<TObjectPtr<AActor>> AllowedExternalReceivers;
};

