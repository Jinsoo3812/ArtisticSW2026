#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWApplyTerrainProfileCommandlet.generated.h"

UCLASS()
class CLASSFEATUREEDITOR_API USWApplyTerrainProfileCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USWApplyTerrainProfileCommandlet();
	virtual int32 Main(const FString& Params) override;
};
