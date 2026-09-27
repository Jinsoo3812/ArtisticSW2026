#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "Crafting/CraftingAccessComponent.h"
#include "Crafting/CraftingComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCraftingPipelineTest,
	"ArtisticSW.Crafting.FullPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCraftingPipelineTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CraftingPipelineTestWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UItemSubsystem* ItemSubsystem = World->GetSubsystem<UItemSubsystem>();
	TestNotNull(TEXT("ItemSubsystem is available"), ItemSubsystem);
	if (!ItemSubsystem)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	ItemSubsystem->ClearCraftingRecipesForTesting();

	FCraftingRecipeRow BasicRecipe;
	BasicRecipe.ResultItemTag = Item_Id_Consumables_Heal_Medicine;
	BasicRecipe.ResultQuantity = 1;
	BasicRecipe.SortOrder = 10;
	FCraftingItemStack WoodCost;
	WoodCost.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
	WoodCost.Quantity = 3;
	BasicRecipe.Ingredients.Add(WoodCost);
	ItemSubsystem->AddCraftingRecipeForTesting(TEXT("Test_Medicine"), BasicRecipe);

	FCraftingRecipeRow LockedRecipe;
	LockedRecipe.ResultItemTag = Item_Id_Weapon_Sword_SwordA4;
	LockedRecipe.RequiredRecipeItemTag = Item_Id_Material_WeaponSpecialRecipe_EpicRecipe;
	FCraftingItemStack IronCost;
	IronCost.ItemTag = Item_Id_Material_WeaponMaterial_GoodIron;
	IronCost.Quantity = 2;
	LockedRecipe.Ingredients.Add(IronCost);
	ItemSubsystem->AddCraftingRecipeForTesting(TEXT("Test_LockedSword"), LockedRecipe);

	TArray<FString> ValidationErrors;
	TestTrue(TEXT("Injected recipes pass validation"), ItemSubsystem->ValidateCraftingRecipes(ValidationErrors));

	AActor* PlayerActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("Crafting owner is spawned"), PlayerActor);
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>(PlayerActor, TEXT("TestInventory"));
	PlayerActor->AddInstanceComponent(Inventory);
	Inventory->RegisterComponent();
	UCraftingComponent* Crafting = NewObject<UCraftingComponent>(PlayerActor, TEXT("TestCrafting"));
	PlayerActor->AddInstanceComponent(Crafting);
	Crafting->RegisterComponent();

	AActor* FacilityActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("Integrated facility actor is spawned"), FacilityActor);
	UCraftingAccessComponent* CraftingAccess = NewObject<UCraftingAccessComponent>(FacilityActor, TEXT("CraftingAccess"));
	FacilityActor->AddInstanceComponent(CraftingAccess);
	CraftingAccess->RegisterComponent();
	World->BeginPlay();
	TestTrue(TEXT("Facility has server authority"), FacilityActor->HasAuthority());
	TestTrue(TEXT("Crafting owner has server authority"), PlayerActor->HasAuthority());
	TestEqual(TEXT("Crafting component is discoverable on owner"), PlayerActor->FindComponentByClass<UCraftingComponent>(), Crafting);
	TestEqual(TEXT("Five wood are added through the preserved inventory path"), Inventory->AddItem(Item_Id_Material_WeaponMaterial_Wood, 5), 5);

	Crafting->OpenCraftingScreen(FacilityActor);
	TestEqual(TEXT("Crafting tab opens with the integrated facility context"), Crafting->GetCurrentCraftingContext(), FacilityActor);

	FCraftingListQuery Query;
	const TArray<FCraftingListEntry> List = Crafting->GetCraftableList(Query);
	TestEqual(TEXT("All enabled recipes are listed"), List.Num(), 2);

	FCraftingDetailsView BasicDetails;
	TestTrue(TEXT("Unlocked recipe details resolve"), Crafting->GetCraftingDetails(TEXT("Test_Medicine"), 1, BasicDetails));
	TestTrue(TEXT("Unlocked ingredients are visible"), BasicDetails.bIngredientsVisible);
	TestEqual(TEXT("Owned quantity is reported"), BasicDetails.Ingredients[0].OwnedQuantity, 5);
	TestEqual(TEXT("Required quantity is reported"), BasicDetails.Ingredients[0].RequiredQuantity, 3);

	FCraftingDetailsView LockedDetails;
	TestTrue(TEXT("Locked recipe details still resolve"), Crafting->GetCraftingDetails(TEXT("Test_LockedSword"), 1, LockedDetails));
	TestEqual(TEXT("Locked recipe reports MissingRecipe"), LockedDetails.Availability, ECraftingAvailability::MissingRecipe);
	TestTrue(TEXT("Locked recipe reports its required recipe item"), LockedDetails.bHasRequiredRecipeItem);
	TestEqual(TEXT("Locked recipe item owned quantity is zero"), LockedDetails.RequiredRecipeItem.OwnedQuantity, 0);
	TestEqual(TEXT("Locked recipe item required quantity is one"), LockedDetails.RequiredRecipeItem.RequiredQuantity, 1);
	TestFalse(TEXT("Locked recipe hides ingredients"), LockedDetails.bIngredientsVisible);
	TestEqual(TEXT("Locked recipe still provides ingredient data"), LockedDetails.Ingredients.Num(), 1);
	TestEqual(TEXT("Locked recipe ingredient quantity is available to specialized UI"),
		LockedDetails.Ingredients[0].RequiredQuantity, 2);

	Inventory->AddItem(Item_Id_Material_WeaponSpecialRecipe_EpicRecipe, 1);
	FCraftingDetailsView UnlockedRecipeDetails;
	TestTrue(TEXT("Unlocked recipe details resolve"), Crafting->GetCraftingDetails(TEXT("Test_LockedSword"), 1, UnlockedRecipeDetails));
	TestTrue(TEXT("Unlocked recipe still reports its required recipe item"), UnlockedRecipeDetails.bHasRequiredRecipeItem);
	TestEqual(TEXT("Unlocked recipe item owned quantity is one"), UnlockedRecipeDetails.RequiredRecipeItem.OwnedQuantity, 1);
	TestTrue(TEXT("Unlocked recipe shows normal ingredients"), UnlockedRecipeDetails.bIngredientsVisible);
	TestEqual(TEXT("Unlocked recipe returns its normal ingredient rows"), UnlockedRecipeDetails.Ingredients.Num(), 1);

	FCraftingRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.RecipeId = TEXT("Test_Medicine");
	Request.CraftCount = 1;
	Request.Output.Type = ECraftingOutputType::Inventory;
	Crafting->RequestCraft(Request);

	TestEqual(TEXT("Craft succeeds"), Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::Success);
	TestEqual(TEXT("Three wood are consumed atomically"), Inventory->GetItemCount(Item_Id_Material_WeaponMaterial_Wood), 2);
	TestEqual(TEXT("One result item is delivered"), Inventory->GetItemCount(Item_Id_Consumables_Heal_Medicine), 1);

	Crafting->RequestCraft(Request);
	TestEqual(TEXT("Duplicate request is rejected"), Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::DuplicateRequest);
	TestEqual(TEXT("Duplicate request consumes nothing"), Inventory->GetItemCount(Item_Id_Material_WeaponMaterial_Wood), 2);
	TestEqual(TEXT("Duplicate request grants nothing"), Inventory->GetItemCount(Item_Id_Consumables_Heal_Medicine), 1);

	FCraftingRequest InsufficientRequest = Request;
	InsufficientRequest.RequestId = FGuid::NewGuid();
	Crafting->RequestCraft(InsufficientRequest);
	TestEqual(TEXT("Insufficient ingredients are rejected"), Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::MissingIngredients);
	TestEqual(TEXT("Failed craft leaves ingredients untouched"), Inventory->GetItemCount(Item_Id_Material_WeaponMaterial_Wood), 2);
	TestEqual(TEXT("Failed craft grants no result"), Inventory->GetItemCount(Item_Id_Consumables_Heal_Medicine), 1);

	FCraftingRecipeRow OversizedRecipe;
	OversizedRecipe.ResultItemTag = Item_Id_Consumables_Heal_Tangyak;
	for (const FGameplayTag IngredientTag : {
		Item_Id_Material_WeaponMaterial_Wood.GetTag(),
		Item_Id_Material_WeaponMaterial_Iron.GetTag(),
		Item_Id_Material_WeaponMaterial_GoodWood.GetTag(),
		Item_Id_Material_WeaponMaterial_GoodIron.GetTag(),
		Item_Id_Material_WeaponSpecialMaterial_EpicMaterial.GetTag()})
	{
		FCraftingItemStack Cost;
		Cost.ItemTag = IngredientTag;
		Cost.Quantity = 1;
		OversizedRecipe.Ingredients.Add(Cost);
	}
	ItemSubsystem->AddCraftingRecipeForTesting(TEXT("Test_Oversized"), OversizedRecipe);
	ValidationErrors.Reset();
	TestFalse(TEXT("Recipes with more than four ingredients fail validation"), ItemSubsystem->ValidateCraftingRecipes(ValidationErrors));
	TestTrue(TEXT("Oversized recipe validation reports the directional slot limit"), ValidationErrors.ContainsByPredicate([](const FString& Error)
	{
		return Error.Contains(TEXT("at most 4"));
	}));
	TestEqual(TEXT("Oversized recipes are omitted from the normal crafting menu"), Crafting->GetCraftableList(Query).Num(), 2);

	FCraftingRequest OversizedRequest;
	OversizedRequest.RequestId = FGuid::NewGuid();
	OversizedRequest.RecipeId = TEXT("Test_Oversized");
	OversizedRequest.CraftCount = 1;
	Crafting->RequestCraft(OversizedRequest);
	TestEqual(TEXT("Server rejects oversized recipes"), Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::InvalidRecipe);

	ItemSubsystem->ClearCraftingRecipesForTesting();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
