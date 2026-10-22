#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWFoleyMigrationLibrary.generated.h"

class UAnimSequenceBase;

/** Explicit, backed-up animation contact migration; source GASP packages are never saved. */
UCLASS()
class CLASSFEATUREEDITOR_API USWFoleyMigrationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Stable native export flags for persisted event metadata comparison. */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|Foley")
    static TArray<FString> ExportNotifyMetadata(UAnimSequenceBase* Sequence);
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|Foley")
    static FString MigrateGaspFoley(const TArray<FString>& Packages, const FString& SourceContent, bool bApply);
};
