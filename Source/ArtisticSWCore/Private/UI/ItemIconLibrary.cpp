#include "UI/ItemIconLibrary.h"
#include "UI/ItemIconSettings.h"
#include "BaseGameplayTags.h"
#include "Item/ItemSubsystem.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

bool UItemIconLibrary::SetItemIcon(UImage* Image, UTexture2D* Texture, FGameplayTag ItemTag)
{
 FGameplayTag Rarity;
 if (Image && ItemTag.IsValid())
 {
  if (UWorld* World = Image->GetWorld())
   if (const UItemSubsystem* Items = World->GetSubsystem<UItemSubsystem>()) Rarity = Items->GetRarityTag(ItemTag);
 }
 return SetItemIconWithRarity(Image, Texture, Rarity);
}

bool UItemIconLibrary::SetItemIconWithRarity(UImage* Image, UTexture2D* Texture, FGameplayTag RarityTag)
{
 if (!Image) return false;
 if (!Texture)
 {
  Image->SetBrushFromTexture(nullptr);
  return false;
 }
 const UItemIconSettings* Settings = GetDefault<UItemIconSettings>();
 UMaterialInterface* Parent = nullptr;
 bool bUsingPreset = false;
 if (Settings->bEnableMaterialIcons)
 {
  const FGameplayTag ResolvedRarity = RarityTag.IsValid() ? RarityTag : FGameplayTag(Item_Rarity_Common);
  if (const TSoftObjectPtr<UMaterialInterface>* Preset = Settings->RarityMaterials.Find(ResolvedRarity))
  {
   Parent = Preset->LoadSynchronous();
   bUsingPreset = Parent != nullptr;
  }
  if (!Parent) Parent = Settings->IconMaterial.LoadSynchronous();
 }
 if (!Parent)
 {
  Image->SetBrushFromTexture(Texture, true);
  return false;
 }
 UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject());
 // Never modify a shared instance or one belonging to another image widget.
 if (!Dynamic || Dynamic->Parent != Parent || Dynamic->GetOuter() != Image)
 {
  Dynamic = UMaterialInstanceDynamic::Create(Parent, Image);
  if (!Dynamic)
  {
   Image->SetBrushFromTexture(Texture, true);
   return false;
  }
  Image->SetBrushFromMaterial(Dynamic);
 }
 Dynamic->SetTextureParameterValue(TEXT("IconTexture"), Texture);
 if (!bUsingPreset)
 {
  FColor Color(0x7C,0x7A,0x75);
  if (RarityTag == Item_Rarity_Relic) Color = FColor(0x62,0x92,0x70);
  else if (RarityTag == Item_Rarity_Rare) Color = FColor(0x4D,0x7D,0xAF);
  else if (RarityTag == Item_Rarity_Epic) Color = FColor(0x8E,0x69,0xAA);
  else if (RarityTag == Item_Rarity_Legendary) Color = FColor(0xC1,0x96,0x40);
  const FLinearColor Linear = FLinearColor::FromSRGBColor(Color);
  Dynamic->SetVectorParameterValue(TEXT("OutlineColor"), Linear);
  Dynamic->SetVectorParameterValue(TEXT("GlowColor"), Linear);
 }
 // Match the previous SetBrushFromTexture(..., true) sizing behavior.
 Image->SetDesiredSizeOverride(FVector2D(Texture->GetSizeX(), Texture->GetSizeY()));
 return true;
}
