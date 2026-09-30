#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWDevTestInputAssetCommandlet.generated.h"
UCLASS()
class USWDevTestInputAssetCommandlet : public UCommandlet
{
 GENERATED_BODY()
public:
 USWDevTestInputAssetCommandlet();
 virtual int32 Main(const FString& Params) override;
};
