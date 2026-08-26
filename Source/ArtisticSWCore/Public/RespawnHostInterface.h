#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RespawnHostInterface.generated.h"

UINTERFACE(BlueprintType)
class ARTISTICSWCORE_API URespawnHostInterface : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API IRespawnHostInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Respawn")
	bool IsAvailableForPlayerRespawn() const;
};
