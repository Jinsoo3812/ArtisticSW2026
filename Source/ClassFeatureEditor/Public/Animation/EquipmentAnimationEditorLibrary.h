#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EquipmentAnimationEditorLibrary.generated.h"
class UAnimMontage;
/** Author upper-body equipment presentations without changing their source sequences. */
UCLASS()
class CLASSFEATUREEDITOR_API UEquipmentAnimationEditorLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable, Category="Animation|Equipment")
 static bool ConfigureEquipMontage(UAnimMontage* Montage, float AttachTime);
};
