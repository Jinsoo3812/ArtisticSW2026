#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayAbility.h"
#include "BaseGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemSubsystem.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/ItemQuickSlotWidget.h"

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
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
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
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ABasePlayerState* State = World->SpawnActor<ABasePlayerState>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	UAbilitySystemComponent* ASC = State->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(State->GetAttributeSet());
	Player->SetPlayerState(State);
	Controller->Possess(Player);
	UItemData* Data = World->GetSubsystem<UItemSubsystem>()->GetItemDataAsset();
	if (!TestNotNull(TEXT("Item registry exists"), Data))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	// Isolate routing/consumption from designer-authored healing effects.
	const FGameplayTag ItemTag = Item_Id_Consumables_Heal_Medicine;
	const FItemDefinition* Existing = Data->ItemDefinitions.Find(ItemTag);
	const TOptional<FItemDefinition> Original = Existing ? TOptional<FItemDefinition>(*Existing) : TOptional<FItemDefinition>();
	FItemDefinition& Definition = Data->ItemDefinitions.FindOrAdd(ItemTag);
	Definition.CategoryTag = Item_Category_Consumable;
	Definition.GrantedAbilityClass = UBaseGameplayAbility::StaticClass();
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

#endif
