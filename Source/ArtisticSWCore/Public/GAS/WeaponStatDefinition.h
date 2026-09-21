#pragma once
#include "CoreMinimal.h"
#include "WeaponStatDefinition.generated.h"

/** Shared authoring data, independent of the attribute implementation. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FWeaponStatDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strength", meta=(ClampMin="0"))
	float StrengthBonus = 0.f;
};
