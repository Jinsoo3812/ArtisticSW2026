#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"

namespace ShipUpgradeMaterialTests
{
	FShipUpgradeNodeDefinition MakeNode(FName NodeId, TArray<FName> Prerequisites = {})
	{
		FShipUpgradeNodeDefinition Node;
		Node.NodeId = NodeId;
		Node.DisplayName = FText::FromName(NodeId);
		Node.PrerequisiteNodeIds = MoveTemp(Prerequisites);
		FShipStatModifier Modifier;
		Modifier.StatType = EShipStatType::MaxHealth;
		Modifier.Operation = EShipStatModifierOperation::AddFlat;
		Modifier.Value = 1.0f;
		Node.StatModifiers.Add(Modifier);
		return Node;
	}

	FCraftingItemStack MakeCost(FGameplayTag ItemTag, int32 Quantity)
	{
		FCraftingItemStack Cost;
		Cost.ItemTag = ItemTag;
		Cost.Quantity = Quantity;
		return Cost;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipUpgradeMaterialPipelineTest,
	"ArtisticSW.ShipUpgrade.MaterialAndMultiParentPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipUpgradeMaterialPipelineTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipUpgradeMaterialPipelineTestWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World)) return false;
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AActor* PlayerActor = World->SpawnActor<AActor>();
	TestNotNull(TEXT("Authoritative player actor is spawned"), PlayerActor);
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>(PlayerActor, TEXT("UpgradeTestInventory"));
	PlayerActor->AddInstanceComponent(Inventory);
	Inventory->RegisterComponent();
	UShipUpgradeComponent* Upgrade = NewObject<UShipUpgradeComponent>(PlayerActor, TEXT("UpgradeTestComponent"));
	PlayerActor->AddInstanceComponent(Upgrade);
	Upgrade->RegisterComponent();

	UShipUpgradeTreeDataAsset* Tree = NewObject<UShipUpgradeTreeDataAsset>();
	Tree->Nodes.Add(ShipUpgradeMaterialTests::MakeNode(TEXT("Root_A")));
	Tree->Nodes.Add(ShipUpgradeMaterialTests::MakeNode(TEXT("Root_B")));
	FShipUpgradeNodeDefinition Child = ShipUpgradeMaterialTests::MakeNode(TEXT("Child_AB"), { TEXT("Root_A"), TEXT("Root_B") });
	Child.ActivationCosts.Add(ShipUpgradeMaterialTests::MakeCost(Item_Id_Material_ShipMaterials_WoodenPlank, 3));
	Child.ActivationCosts.Add(ShipUpgradeMaterialTests::MakeCost(Item_Id_Material_ShipMaterials_IronPlate, 2));
	Tree->Nodes.Add(MoveTemp(Child));
	Upgrade->ConfigureForUseCase(Tree, FShipStatSnapshot(), false);
	World->BeginPlay();

	TestEqual(TEXT("Five wooden planks are added"), Inventory->AddItem(Item_Id_Material_ShipMaterials_WoodenPlank, 5), 5);
	TestEqual(TEXT("One iron plate is added"), Inventory->AddItem(Item_Id_Material_ShipMaterials_IronPlate, 1), 1);
	TestEqual(TEXT("Child starts locked while both parents are inactive"), Upgrade->GetNodeState(TEXT("Child_AB")), EShipUpgradeNodeState::Locked);
	TestEqual(TEXT("First parent activates"), Upgrade->ActivateNodeForUseCase(TEXT("Root_A")), EShipUpgradeActivationResult::Success);
	TestEqual(TEXT("Child remains locked until every configured parent is active"), Upgrade->GetNodeState(TEXT("Child_AB")), EShipUpgradeNodeState::Locked);
	TestEqual(TEXT("Second parent activates"), Upgrade->ActivateNodeForUseCase(TEXT("Root_B")), EShipUpgradeActivationResult::Success);
	TestEqual(TEXT("Child becomes graph-available after all parents activate"), Upgrade->GetNodeState(TEXT("Child_AB")), EShipUpgradeNodeState::Available);

	FText Reason;
	TestFalse(TEXT("Material check reports insufficient inventory"), Upgrade->HasRequiredMaterials(TEXT("Child_AB"), Reason));
	FShipUpgradeNodeView View;
	TestTrue(TEXT("UI node view resolves"), Upgrade->GetNodeView(TEXT("Child_AB"), View));
	TestEqual(TEXT("UI exposes both material rows"), View.MaterialCosts.Num(), 2);
	TestFalse(TEXT("UI exposes aggregate affordability"), View.bHasEnoughMaterials);
	TestEqual(TEXT("Activation with missing material is rejected"), Upgrade->ActivateNodeForUseCase(TEXT("Child_AB")), EShipUpgradeActivationResult::MissingMaterials);
	TestEqual(TEXT("Failed activation consumes no wood"), Inventory->GetItemCount(Item_Id_Material_ShipMaterials_WoodenPlank), 5);
	TestEqual(TEXT("Failed activation consumes no iron"), Inventory->GetItemCount(Item_Id_Material_ShipMaterials_IronPlate), 1);

	TestEqual(TEXT("Missing iron is added"), Inventory->AddItem(Item_Id_Material_ShipMaterials_IronPlate, 1), 1);
	TestTrue(TEXT("Material check succeeds once all costs are owned"), Upgrade->HasRequiredMaterials(TEXT("Child_AB"), Reason));
	TestEqual(TEXT("Affordable node activates"), Upgrade->ActivateNodeForUseCase(TEXT("Child_AB")), EShipUpgradeActivationResult::Success);
	TestEqual(TEXT("Wood cost is consumed atomically"), Inventory->GetItemCount(Item_Id_Material_ShipMaterials_WoodenPlank), 2);
	TestEqual(TEXT("Iron cost is consumed atomically"), Inventory->GetItemCount(Item_Id_Material_ShipMaterials_IronPlate), 0);
	TestEqual(TEXT("Duplicate activation is idempotent"), Upgrade->ActivateNodeForUseCase(TEXT("Child_AB")), EShipUpgradeActivationResult::AlreadyActive);
	TestEqual(TEXT("Duplicate activation consumes nothing further"), Inventory->GetItemCount(Item_Id_Material_ShipMaterials_WoodenPlank), 2);

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
