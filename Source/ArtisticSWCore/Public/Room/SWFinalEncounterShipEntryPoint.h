#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWFinalEncounterShipEntryPoint.generated.h"

class UArrowComponent;

UCLASS()
class ARTISTICSWCORE_API ASWFinalEncounterShipEntryPoint : public AActor
{
	GENERATED_BODY()
public:
	ASWFinalEncounterShipEntryPoint();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entry")
	TObjectPtr<UArrowComponent> Direction;
};
