#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "StatusReceiver.generated.h"

/** Opt-in contract for actors/components which accept character status effects. */
UINTERFACE(BlueprintType)
class ARTISTICSWCORE_API UStatusReceiver : public UInterface
{
	GENERATED_BODY()
};
class ARTISTICSWCORE_API IStatusReceiver
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "GAS|Status")
	bool CanReceiveStatus(FGameplayTag StatusTag) const;
};
