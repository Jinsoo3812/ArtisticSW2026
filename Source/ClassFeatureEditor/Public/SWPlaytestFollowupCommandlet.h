#pragma once
#include "Commandlets/Commandlet.h"
#include "SWPlaytestFollowupCommandlet.generated.h"
UCLASS()
class USWPlaytestFollowupCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
