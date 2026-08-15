#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "Inventory/InventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDialogueInventoryTransactionTest,
	"ArtisticSW.NPCDialogue.InventoryTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDialogueInventoryTransactionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DialogueInventoryTransactionWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AActor* Player = World->SpawnActor<AActor>();
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>(Player, TEXT("DialogueInventory"));
	Player->AddInstanceComponent(Inventory);
	Inventory->RegisterComponent();
	World->BeginPlay();

	TestEqual(TEXT("Input items are added"),
		Inventory->AddItem(Item_Id_Material_WeaponMaterial_Wood, 5), 5);
	FCraftingItemStack Cost;
	Cost.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
	Cost.Quantity = 3;
	FCraftingItemStack Reward;
	Reward.ItemTag = Item_Id_Consumables_Heal_Medicine;
	Reward.Quantity = 1;
	TArray<FCraftingItemStack> Costs{ Cost };
	TArray<FCraftingItemStack> Rewards{ Reward };

	TestTrue(TEXT("Combined dialogue transaction preflight succeeds"),
		Inventory->CanApplyItemTransaction(Costs, Rewards));
	TestTrue(TEXT("Combined dialogue transaction commits"),
		Inventory->TryApplyItemTransaction(Costs, Rewards));
	TestEqual(TEXT("Cost is removed"),
		Inventory->GetItemCount(Item_Id_Material_WeaponMaterial_Wood), 2);
	TestEqual(TEXT("Reward is granted"),
		Inventory->GetItemCount(Item_Id_Consumables_Heal_Medicine), 1);

	Cost.Quantity = 3;
	Costs = { Cost };
	TestFalse(TEXT("Insufficient cost fails preflight"),
		Inventory->CanApplyItemTransaction(Costs, Rewards));
	TestFalse(TEXT("Failed combined transaction does not commit"),
		Inventory->TryApplyItemTransaction(Costs, Rewards));
	TestEqual(TEXT("Failed transaction preserves remaining cost items"),
		Inventory->GetItemCount(Item_Id_Material_WeaponMaterial_Wood), 2);
	TestEqual(TEXT("Failed transaction grants no duplicate reward"),
		Inventory->GetItemCount(Item_Id_Consumables_Heal_Medicine), 1);

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
