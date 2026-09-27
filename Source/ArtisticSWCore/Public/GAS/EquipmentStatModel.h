#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EquipmentStatModel.generated.h"

/** Read-only contract for item actors. Effect ownership stays in GASCore. */
UCLASS(Abstract)
class ARTISTICSWCORE_API UEquipmentStatModel : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual bool IsEquipped(const AActor* Item) const PURE_VIRTUAL(UEquipmentStatModel::IsEquipped, return false;);
};
