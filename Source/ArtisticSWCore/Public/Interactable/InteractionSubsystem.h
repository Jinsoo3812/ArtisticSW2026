#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Interactable/InteractionData.h"
#include "Subsystems/WorldSubsystem.h"
#include "InteractionSubsystem.generated.h"

UCLASS()
class ARTISTICSWCORE_API UInteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const FInteractionFeatureData* GetInteractionFeature(FGameplayTag InteractableIdTag) const;

private:
	TMap<FGameplayTag, FInteractionFeatureData> CachedInteractionFeatures;
};
