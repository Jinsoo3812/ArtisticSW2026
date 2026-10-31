#include "UI/ItemIconSettings.h"
#include "BaseGameplayTags.h"
#include "Materials/MaterialInterface.h"
UItemIconSettings::UItemIconSettings()
{
 IconMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/jiwon/UI_Material/M_IconEffect.M_IconEffect")));
 auto Add = [this](FGameplayTag Tag, const TCHAR* Name)
 {
  const FString Path = FString::Printf(TEXT("/Game/jiwon/UI_Material/IconEffectInstances/Rarity/MI_IconEffect_%s.MI_IconEffect_%s"), Name, Name);
  RarityMaterials.Add(Tag, TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Path)));
 };
 Add(Item_Rarity_Common, TEXT("Common"));
 Add(Item_Rarity_Relic, TEXT("Relic"));
 Add(Item_Rarity_Rare, TEXT("Rare"));
 Add(Item_Rarity_Epic, TEXT("Epic"));
 Add(Item_Rarity_Legendary, TEXT("Legendary"));
}
