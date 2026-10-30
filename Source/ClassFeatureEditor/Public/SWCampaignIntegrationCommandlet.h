#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWCampaignIntegrationCommandlet.generated.h"

UCLASS()
class USWCampaignIntegrationCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USWCampaignIntegrationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
