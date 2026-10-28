#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWFoleyMigrationLibrary.generated.h"

class UAnimSequenceBase;
class UAnimNotify;
class UBlendSpace;

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
    /** Adds one enemy-only marker. Caller owns backups, pose analysis and saving. */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|Foley")
    static bool AddEnemyContact(UAnimSequenceBase* Sequence, TSubclassOf<UAnimNotify> NotifyClass, float Time, bool bLeft);
    /** Adds an idempotent, client-only hand-catch marker; caller backs up and saves. */
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|Foley")
    static bool AddSwimContact(UAnimSequenceBase* Sequence, float Time, bool bLeft);
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|Foley")
    static TArray<FString> InspectFunctions(UObject* Object, const FString& NameFilter);
    UFUNCTION(BlueprintCallable, Category="Artistic SW|Authoring|Foley")
    static void RebuildContactBlendSpace(UBlendSpace* BlendSpace);
};
