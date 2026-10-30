#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "ItemIconSettings.generated.h"
class UMaterialInterface;

/** Shared UI appearance; does not change item definitions or gameplay. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Item Icon Appearance"))
class ARTISTICSWCORE_API UItemIconSettings : public UDeveloperSettings
{
 GENERATED_BODY()
public:
 UItemIconSettings();
 virtual FName GetCategoryName() const override { return TEXT("Game"); }
 UPROPERTY(Config, EditAnywhere, Category="Appearance")
 bool bEnableMaterialIcons = true;
 UPROPERTY(Config, EditAnywhere, Category="Appearance")
 TSoftObjectPtr<UMaterialInterface> IconMaterial;
 /** Material presets supply OutlineColor/GlowColor and inherit master effects. */
 UPROPERTY(Config, EditAnywhere, Category="Appearance", meta=(Categories="Item.Rarity"))
 TMap<FGameplayTag, TSoftObjectPtr<UMaterialInterface>> RarityMaterials;
};
