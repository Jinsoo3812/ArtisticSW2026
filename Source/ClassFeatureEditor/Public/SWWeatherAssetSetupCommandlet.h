#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWWeatherAssetSetupCommandlet.generated.h"

UCLASS()
class CLASSFEATUREEDITOR_API USWWeatherAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
