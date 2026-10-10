#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShipCrewAuthoringCommandlet.generated.h"

UCLASS()
class ENEMY_API UShipCrewAuthoringCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UShipCrewAuthoringCommandlet();
	virtual int32 Main(const FString& Params) override;
};
