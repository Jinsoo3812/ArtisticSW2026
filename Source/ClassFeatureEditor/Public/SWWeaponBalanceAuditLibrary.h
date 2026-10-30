#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWWeaponBalanceAuditLibrary.generated.h"

class UAnimMontage;

/** Read-only timing export for weapon balance; never edits animation assets. */
UCLASS()
class CLASSFEATUREEDITOR_API USWWeaponBalanceAuditLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Weapon|Audit")
	static FString InspectMontage(UAnimMontage* Montage);
};
