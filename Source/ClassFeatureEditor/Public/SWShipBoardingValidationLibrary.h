#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWShipBoardingValidationLibrary.generated.h"
class ACharacter;
class AShip;

/** PIE-only commands run next game tick so input/RPCs use the normal engine path. */
UCLASS()
class CLASSFEATUREEDITOR_API USWShipBoardingValidationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintCallable, Category = "Ship|Validation")
	static bool QueueSeaBoard(ACharacter* Character, AShip* Ship, bool bStarboard);
	UFUNCTION(BlueprintCallable, Category = "Ship|Validation")
	static bool QueueRelocate(ACharacter* Character, FVector Location);
	UFUNCTION(BlueprintCallable, Category = "Ship|Validation")
	static FString DescribeRollState(ACharacter* Character);
};
