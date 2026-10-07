#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWBuildLandscapeNaniteCommandlet.generated.h"

/** Build, validate and save Nanite data for one explicitly selected non-partitioned map. */
UCLASS()
class USWBuildLandscapeNaniteCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USWBuildLandscapeNaniteCommandlet();
	virtual int32 Main(const FString& Params) override;
};
