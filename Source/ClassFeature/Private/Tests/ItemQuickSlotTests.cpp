#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayAbility.h"
#include "BaseGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "BasePlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemSubsystem.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/ItemQuickSlotWidget.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Item/BaseItem.h"
#include "Equipment/PlayerEquipmentComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FItemQuickSlotWidgetAssetTest,
	"ArtisticSW.UI.ItemQuickSlot.WidgetAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FItemQuickSlotWidgetAssetTest::RunTest(const FString& Parameters)
{
	UClass* WidgetClass = LoadClass<UItemQuickSlotWidget>(
		nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_ItemQuickSlot/WBP_ItemQuickSlot.WBP_ItemQuickSlot_C"));
	if (!TestNotNull(TEXT("WBP_ItemQuickSlot generated class is loadable"), WidgetClass))
	{
		return false;
	}

	const UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetClass);
	const UWidgetTree* Tree = GeneratedClass ? GeneratedClass->GetWidgetTreeArchetype() : nullptr;
	if (!TestNotNull(TEXT("WBP_ItemQuickSlot has a designer widget tree"), Tree))
	{
		return false;
	}

	const FName RequiredWidgetNames[] =
	{
		TEXT("ItemIconImage3"), TEXT("ItemNameText3"), TEXT("CountText3"), TEXT("PressedHighlightBorder3"), TEXT("ItemInfoOverlay3"),
		TEXT("ItemIconImage4"), TEXT("ItemNameText4"), TEXT("CountText4"), TEXT("PressedHighlightBorder4"), TEXT("ItemInfoOverlay4"),
		TEXT("ItemIconImage5"), TEXT("ItemNameText5"), TEXT("CountText5"), TEXT("PressedHighlightBorder5"), TEXT("ItemInfoOverlay5")
	};
	for (const FName WidgetName : RequiredWidgetNames)
	{
		TestNotNull(*FString::Printf(TEXT("Required designer variable exists: %s"), *WidgetName.ToString()),
			Tree->FindWidget(WidgetName));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConsumableQuickSlotSelectionTest,
	"ArtisticSW.UI.ItemQuickSlot.PersistentSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConsumableQuickSlotSelectionTest::RunTest(const FString& Parameters)
{
	// Existing shared crafting data is unrelated to quick-slot input.

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ItemQuickSlotHoldStateWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	if (!TestNotNull(TEXT("Player is spawned"), Player))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	Player->QuickSlots.SetNum(5);
	for (int32 Index = 0; Index < Player->QuickSlots.Num(); ++Index)
	{
		Player->QuickSlots[Index].SlotType = Index < 2
			? EQuickSlotType::Weapon
			: EQuickSlotType::Consumable;
	}

	int32 ChangeCount = 0;
	Player->OnConsumableQuickSlotInputChanged.AddLambda([&ChangeCount]()
	{
		++ChangeCount;
	});

	Player->BeginConsumableQuickSlotInput(2);
	TestEqual(TEXT("Pressing slot 3 highlights quick-slot index 2"),
		Player->GetPressedConsumableQuickSlotIndex(), 2);

	Player->BeginConsumableQuickSlotInput(3);
	TestEqual(TEXT("The most recently pressed consumable slot is highlighted"),
		Player->GetPressedConsumableQuickSlotIndex(), 3);

	Player->EndConsumableQuickSlotInput(3);
	TestEqual(TEXT("Releasing slot 4 preserves its selection"),
		Player->GetSelectedConsumableQuickSlotIndex(), 3);

	Player->EndConsumableQuickSlotInput(2);
	TestEqual(TEXT("Releasing an older key does not change the selected slot"),
		Player->GetSelectedConsumableQuickSlotIndex(), 3);
	TestEqual(TEXT("Only selection changes broadcast a visual state change"), ChangeCount, 2);

	Player->BeginConsumableQuickSlotInput(0);
	TestEqual(TEXT("Invalid selection leaves the selected consumable unchanged"),
		Player->GetSelectedConsumableQuickSlotIndex(), 3);
	Player->ActivateQuickSlot(0);
	TestEqual(TEXT("Selecting a weapon slot clears the consumable selection"),
		Player->GetPressedConsumableQuickSlotIndex(), INDEX_NONE);

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConsumableQuickSlotUseTest,
	"ArtisticSW.UI.ItemQuickSlot.FUsePriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConsumableQuickSlotUseTest::RunTest(const FString& Parameters)
{

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ABasePlayerState* State = World->SpawnActor<ABasePlayerState>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	ABasePlayerController* Controller = World->SpawnActor<ABasePlayerController>();
	UAbilitySystemComponent* ASC = State->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(State->GetAttributeSet());
	Player->SetPlayerState(State);
	Controller->PlayerState = State;
	Controller->Possess(Player);
	UItemData* Data = World->GetSubsystem<UItemSubsystem>()->GetItemDataAsset();
	if (!TestNotNull(TEXT("Item registry exists"), Data))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	// Use a valid recovery request to test F routing; full-health recovery is rejected.
	const FGameplayTag ItemTag = Item_Id_Consumables_Heal_Medicine;
	const FItemDefinition* Existing = Data->ItemDefinitions.Find(ItemTag);
	const TOptional<FItemDefinition> Original = Existing ? TOptional<FItemDefinition>(*Existing) : TOptional<FItemDefinition>();
	FItemDefinition& Definition = Data->ItemDefinitions.FindOrAdd(ItemTag);
	Definition.CategoryTag = Item_Category_Consumable;
	Definition.Consumable.Effect = EConsumableEffect::InstantHeal;
	Definition.Consumable.Healing = 25.f;
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 100.f);
	UInventoryComponent* Inventory = Player->GetInventoryComponent();
	TestEqual(TEXT("Two fixture consumables added"), Inventory->AddItem(ItemTag, 2), 2);
	Player->QuickSlots.SetNum(5);
	Player->QuickSlots[2].SlotType = EQuickSlotType::Consumable;
	Player->QuickSlots[2].ItemTag = ItemTag;
	FGameplayAbilitySpec InteractionSpec(UBaseGameplayAbility::StaticClass(), 1);
	InteractionSpec.GetDynamicSpecSourceTags().AddTag(Key_Default_F);
	const FGameplayAbilitySpecHandle InteractionHandle = ASC->GiveAbility(InteractionSpec);
	Player->BeginConsumableQuickSlotInput(2);
	Player->EndConsumableQuickSlotInput(2);
	TestEqual(TEXT("Selecting and releasing 3 consumes nothing"), Inventory->GetMaterialCount(ItemTag), 2);
	TestTrue(TEXT("Selected consumable owns F"), Player->HasSelectedQuickSlotConsumable());
	Player->OnAbilityInputPressed(Key_Default_F);
	TestEqual(TEXT("F consumes exactly one selected item"), Inventory->GetMaterialCount(ItemTag), 1);
	TestFalse(TEXT("F does not also trigger an interaction ability"), ASC->FindAbilitySpecFromHandle(InteractionHandle)->InputPressed);
	TestEqual(TEXT("Use preserves selection"), Player->GetSelectedConsumableQuickSlotIndex(), 2);
	Player->ResetConsumableQuickSlotInputs();
	Player->OnAbilityInputPressed(Key_Default_F);
	TestTrue(TEXT("F returns to interaction after deselection"), ASC->FindAbilitySpecFromHandle(InteractionHandle)->InputPressed);
	if (Original.IsSet()) Data->ItemDefinitions.Add(ItemTag, Original.GetValue());
	else Data->ItemDefinitions.Remove(ItemTag);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepairQuickSlotPresentationTest,
	"ArtisticSW.UI.ItemQuickSlot.RepairHeldMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRepairQuickSlotPresentationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	ABasePlayerState* State = World->SpawnActor<ABasePlayerState>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	ABasePlayerController* Controller = World->SpawnActor<ABasePlayerController>();
	State->GetAbilitySystemComponent()->AddAttributeSetSubobject(State->GetAttributeSet());
	Player->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/jiwon/Characters/SKM_Player_Woman.SKM_Player_Woman")));
	Player->SetPlayerState(State);
	Controller->PlayerState = State;
	Controller->Possess(Player);
	Player->QuickSlots.SetNum(5);
	const TArray<FGameplayTag> Tags = {Item_Id_Material_ShipMaterials_WoodenPlank, Item_Id_Material_ShipMaterials_GoodWoodenPlank,
		Item_Id_Material_ShipMaterials_IronPlate, Item_Id_Material_ShipMaterials_GoodIronPlate};
	for (int32 Index = 0; Index < Tags.Num(); ++Index)
	{
		World->Tick(LEVELTICK_All, 0.2f);
		const int32 Slot = 2 + Index % 3;
		Player->GetInventoryComponent()->AddItem(Tags[Index], 1);
		Player->QuickSlots[Slot].SlotType = EQuickSlotType::Consumable;
		Player->QuickSlots[Slot].ItemTag = Tags[Index];
		Player->BeginConsumableQuickSlotInput(Slot);
		ABaseItem* Item = Player->EquippedItem;
		if (!TestNotNull(Tags[Index].ToString() + TEXT(" is held after selecting 3/4/5"), Item)) continue;
		TestEqual(TEXT("Held tag matches selection"), Item->ItemTag, Tags[Index]);
		UStaticMeshComponent* Mesh = Item->FindComponentByClass<UStaticMeshComponent>();
		TestTrue(TEXT("Held mesh exists and is visible"), Mesh && Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->bHiddenInGame && !Item->IsHidden());
		TestEqual(TEXT("Held item attaches to player skeletal mesh"), Item->GetRootComponent()->GetAttachParent(), static_cast<USceneComponent*>(Player->GetMesh()));
		const FName Socket = Item->GetRootComponent()->GetAttachSocketName();
		TestTrue(TEXT("Configured hand socket exists"), Player->GetMesh()->DoesSocketExist(Socket));
		TestTrue(TEXT("Held item is at configured hand socket"), Item->GetActorLocation().Equals(Player->GetMesh()->GetSocketLocation(Socket), 0.1f));
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
