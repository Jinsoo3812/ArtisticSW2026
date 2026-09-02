#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "ShipRepairUserInterface.generated.h"

class UShipRepairPointComponent;

UINTERFACE(MinimalAPI)
class UShipRepairUserInterface : public UInterface
{
	GENERATED_BODY()
};

/** Native bridge that keeps the ship module independent from the player/inventory module. */
class ARTISTICSWCORE_API IShipRepairUserInterface
{
	GENERATED_BODY()

public:
	virtual bool GetEquippedShipRepairMaterial(FGameplayTag& OutItemTag) const = 0;
	virtual bool IsShipRepairInputHeld() const = 0;
	virtual bool ConsumeShipRepairMaterial(FGameplayTag ItemTag) = 0;
	virtual void BeginShipRepair(UShipRepairPointComponent* RepairPoint, float Duration) = 0;
	virtual void EndShipRepair(UShipRepairPointComponent* RepairPoint, bool bCompleted) = 0;
};
