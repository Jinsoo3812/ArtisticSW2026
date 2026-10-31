#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWAssetRepairLibrary.generated.h"

/** Explicit, editor-only package migration. Does not change gameplay on load. */
UCLASS()
class CLASSFEATUREEDITOR_API USWAssetRepairLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Create a standard redirect at an absent historical path; never overwrite an asset. */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Asset Repair")
    static bool CreateHistoricalRedirector(const FString& PackageName, UObject* Destination);

    /** Stable graph/default snapshot for checking editor-only serialization repairs. */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Asset Repair")
    static FString DescribeBlueprintState(class UBlueprint* Blueprint);
};
