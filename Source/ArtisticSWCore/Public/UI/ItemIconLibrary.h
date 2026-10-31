#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "ItemIconLibrary.generated.h"
class UImage;
class UTexture2D;

UCLASS()
class ARTISTICSWCORE_API UItemIconLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Returns true when a material was used; null icons clear the old brush. */
 UFUNCTION(BlueprintCallable, Category="UI|Items")
 static bool SetItemIcon(UImage* Image, UTexture2D* Texture, FGameplayTag ItemTag);
 UFUNCTION(BlueprintCallable, Category="UI|Items")
 static bool SetItemIconWithRarity(UImage* Image, UTexture2D* Texture, FGameplayTag RarityTag);
};
