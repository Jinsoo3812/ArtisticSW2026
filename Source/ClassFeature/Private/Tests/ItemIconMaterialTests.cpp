#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/ItemIconLibrary.h"
#include "UI/ItemIconSettings.h"
#include "UI/InventoryEntryWidget.h"
#include "BaseGameplayTags.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Item/ItemSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "UI/Crafting/CraftingIngredientEntryWidget.h"

namespace ItemIconTests
{
 UTexture* TextureValue(UMaterialInstanceDynamic* Material)
 {
  UTexture* Texture = nullptr;
  if (Material) Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("IconTexture")), Texture);
  return Texture;
 }
 UTexture2D* LoadIcon(const TCHAR* Name)
 {
  return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/jiwon/UI_Material/ItemTexture/%s.%s"),Name,Name));
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemIconMaterialIsolationTest,
 "ArtisticSW.UI.ItemIcon.MaterialIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FItemIconMaterialIsolationTest::RunTest(const FString&)
{
 UImage* First = NewObject<UImage>();
 UImage* Second = NewObject<UImage>();
 UTexture2D* Wood = ItemIconTests::LoadIcon(TEXT("ItemUI_01_Wood"));
 UTexture2D* Pear = ItemIconTests::LoadIcon(TEXT("ItemUI_05_Sail"));
 if (!TestNotNull(TEXT("Wood icon"),Wood) || !TestNotNull(TEXT("Pear icon"),Pear)) return false;
 First->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 First->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f));
 TestTrue(TEXT("Common icon uses effect material"),UItemIconLibrary::SetItemIconWithRarity(First,Wood,Item_Rarity_Common));
 TestTrue(TEXT("Placeholder black tint is removed on first icon assignment"), First->GetColorAndOpacity().Equals(FLinearColor::White));
 TestTrue(TEXT("Rare icon uses effect material"),UItemIconLibrary::SetItemIconWithRarity(Second,Pear,Item_Rarity_Rare));
 UMaterialInstanceDynamic* A = Cast<UMaterialInstanceDynamic>(First->GetBrush().GetResourceObject());
 UMaterialInstanceDynamic* B = Cast<UMaterialInstanceDynamic>(Second->GetBrush().GetResourceObject());
 if (!TestNotNull(TEXT("First dynamic material"),A) || !TestNotNull(TEXT("Second dynamic material"),B)) return false;
 TestTrue(TEXT("Icons own separate material instances"),A!=B && A->GetOuter()==First && B->GetOuter()==Second);
 TestTrue(TEXT("Different rarity chooses different preset parents"),A->Parent!=B->Parent);
 TestTrue(TEXT("First icon texture"),ItemIconTests::TextureValue(A)==Wood);
 TestTrue(TEXT("Second icon texture"),ItemIconTests::TextureValue(B)==Pear);
 UItemIconLibrary::SetItemIconWithRarity(First,Pear,Item_Rarity_Common);
 TestTrue(TEXT("Refreshing the same rarity reuses the image's material"),First->GetBrush().GetResourceObject()==A);
 TestTrue(TEXT("Texture is updated when a slot is reused"),ItemIconTests::TextureValue(A)==Pear);
 TestTrue(TEXT("Another image keeps its texture"),ItemIconTests::TextureValue(B)==Pear);
 TestEqual(TEXT("Hit test policy is preserved"),First->GetVisibility(),ESlateVisibility::SelfHitTestInvisible);
 for (const FGameplayTag Tag : {FGameplayTag(Item_Rarity_Common),FGameplayTag(Item_Rarity_Relic),FGameplayTag(Item_Rarity_Rare),FGameplayTag(Item_Rarity_Epic),FGameplayTag(Item_Rarity_Legendary)})
 {
  TestTrue(TEXT("Each configured grade loads"),UItemIconLibrary::SetItemIconWithRarity(First,Wood,Tag));
  UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(First->GetBrush().GetResourceObject());
  if (!Dynamic) continue;
  UMaterialInterface* Parent = GetDefault<UItemIconSettings>()->RarityMaterials.FindChecked(Tag).LoadSynchronous();
  TestTrue(TEXT("Designer preset is the runtime parent"),Dynamic->Parent==Parent);
  FLinearColor Actual,Expected;
  Dynamic->GetVectorParameterValue(FMaterialParameterInfo(TEXT("OutlineColor")),Actual);
  Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("OutlineColor")),Expected);
  TestTrue(TEXT("Outline color inherits designer settings"),Actual.Equals(Expected));
 }
 UItemIconLibrary::SetItemIconWithRarity(First,nullptr,Item_Rarity_Common);
 TestNull(TEXT("Clearing a slot removes the previous material and texture"),First->GetBrush().GetResourceObject());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemIconFallbackTest,
 "ArtisticSW.UI.ItemIcon.TextureFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FItemIconFallbackTest::RunTest(const FString&)
{
 UItemIconSettings* Settings = GetMutableDefault<UItemIconSettings>();
 const bool Enabled = Settings->bEnableMaterialIcons;
 ON_SCOPE_EXIT { Settings->bEnableMaterialIcons = Enabled; };
 Settings->bEnableMaterialIcons = false;
 UImage* Image = NewObject<UImage>();
 UTexture2D* Texture = UTexture2D::CreateTransient(16,16);
 TestFalse(TEXT("Disabled styling uses original texture mode"),UItemIconLibrary::SetItemIconWithRarity(Image,Texture,Item_Rarity_Epic));
 TestTrue(TEXT("Texture fallback stays visible through the original resource"),Image->GetBrush().GetResourceObject()==Texture);
 TestFalse(TEXT("Null image is safe"),UItemIconLibrary::SetItemIconWithRarity(nullptr,Texture,Item_Rarity_Epic));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchIngredientIconTest,
 "ArtisticSW.UI.ItemIcon.WorkbenchFirstDisplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkbenchIngredientIconTest::RunTest(const FString&)
{
 UTexture2D* Wood = ItemIconTests::LoadIcon(TEXT("ItemUI_01_Wood"));
 if (!TestNotNull(TEXT("Ingredient icon"), Wood)) return false;
 for (const TCHAR* Path : {
  TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_Crafting/WBP_CraftingIngredientEntry.WBP_CraftingIngredientEntry_C"),
  TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_SkillUpgrade/WBP_SkillIngredientEntry.WBP_SkillIngredientEntry_C")})
 {
  UClass* WidgetClass = LoadClass<UCraftingIngredientEntryWidget>(nullptr, Path);
  if (!TestNotNull(TEXT("Authored ingredient class"), WidgetClass)) continue;
  UCraftingIngredientEntryWidget* Entry = NewObject<UCraftingIngredientEntryWidget>(GetTransientPackage(), WidgetClass);
  Entry->Initialize();
  UImage* Image = Cast<UImage>(Entry->WidgetTree->FindWidget(TEXT("IngredientIconImage")));
  if (!TestNotNull(TEXT("Ingredient image"), Image)) continue;
  AddInfo(FString::Printf(TEXT("%s initial Color=%s"), Path, *Image->GetColorAndOpacity().ToString()));
  FCraftingIngredientView Ingredient;
  Ingredient.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
  Ingredient.Icon = Wood;
  // Production applies ingredient data before adding the entry to its parent.
  Entry->SetupFromIngredient(Ingredient);
  auto SlateWidget = Entry->TakeWidget();
  TestTrue(TEXT("First constructed ingredient icon is untinted"), Image->GetColorAndOpacity().Equals(FLinearColor::White));
  TestTrue(TEXT("First constructed ingredient has the correct texture"),
   ItemIconTests::TextureValue(Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject())) == Wood);
  Entry->SetupFromIngredient(Ingredient);
  TestTrue(TEXT("Refresh keeps the same visible tint"), Image->GetColorAndOpacity().Equals(FLinearColor::White));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemIconCatalogIntegrationTest,
 "ArtisticSW.UI.ItemIcon.InventoryWidgetIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FItemIconCatalogIntegrationTest::RunTest(const FString&)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game,false,TEXT("ItemIconIntegrationWorld"));
 if (!TestNotNull(TEXT("Game world"),World)) return false;
 FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
 Context.SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 UItemSubsystem* Items=World->GetSubsystem<UItemSubsystem>();
 if (!TestNotNull(TEXT("Item subsystem"),Items)) return false;
 UClass* Class=LoadClass<UInventoryEntryWidget>(nullptr,TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_Inventory/WBP_InventoryEntry.WBP_InventoryEntry_C"));
 if (!TestNotNull(TEXT("Authored inventory entry"),Class)) return false;
 UInventoryEntryWidget* Entry=NewObject<UInventoryEntryWidget>(World,Class);
 Entry->Initialize();
 UImage* Image=Cast<UImage>(Entry->WidgetTree->FindWidget(TEXT("ItemIconImage")));
 if (!TestNotNull(TEXT("Designer image binding"),Image)) return false;
 const FGameplayTag Tags[]={Item_Id_Material_WeaponMaterial_Wood,Item_Id_Material_WeaponMaterial_GoodWood};
 for (FGameplayTag Tag : Tags)
 {
  UTexture2D* Icon=Items->GetIcon2D(Tag).LoadSynchronous();
  Entry->SetupFromData(Items->GetItemName(Tag),3,Icon,0,Tag,FText::GetEmpty());
  UMaterialInstanceDynamic* Dynamic=Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject());
  if (!TestNotNull(TEXT("Inventory entry displays a dynamic effect material"),Dynamic)) return false;
  TestTrue(TEXT("Catalog texture reaches the material"),ItemIconTests::TextureValue(Dynamic)==Icon);
  const FGameplayTag Rarity=Items->GetRarityTag(Tag);
  TestTrue(TEXT("Catalog rarity chooses the designer preset"),Dynamic->Parent==GetDefault<UItemIconSettings>()->RarityMaterials.FindChecked(Rarity).LoadSynchronous());
 }
 Entry->SetupAsEmpty(0);
 TestEqual(TEXT("Empty inventory slots remain hidden"),Image->GetVisibility(),ESlateVisibility::Hidden);
 return true;
}
#endif

