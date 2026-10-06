#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWRoomAssetSetupCommandlet.generated.h"

/** One-shot editor validation and authoring of the hosted room map and input assets. */
UCLASS()
class CLASSFEATUREEDITOR_API USWRoomAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
