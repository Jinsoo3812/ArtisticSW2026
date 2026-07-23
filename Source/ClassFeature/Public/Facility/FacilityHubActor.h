#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityHubActor.generated.h"

/**
 * Runtime parent for the integrated facility Blueprint.
 * The Blueprint owns its mesh, interaction volume, and feature access components.
 */
UCLASS()
class CLASSFEATURE_API AFacilityHubActor : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleInteracted(AActor* Interactor);
};
