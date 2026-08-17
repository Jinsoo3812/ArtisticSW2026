#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BasePlayer.h"
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
		TEXT("ItemIconImage3"), TEXT("ItemNameText3"), TEXT("CountText3"), TEXT("KeyText3"), TEXT("PressedHighlightBorder3"), TEXT("ItemInfoOverlay3"),
		TEXT("ItemIconImage4"), TEXT("ItemNameText4"), TEXT("CountText4"), TEXT("KeyText4"), TEXT("PressedHighlightBorder4"), TEXT("ItemInfoOverlay4"),
		TEXT("ItemIconImage5"), TEXT("ItemNameText5"), TEXT("CountText5"), TEXT("KeyText5"), TEXT("PressedHighlightBorder5"), TEXT("ItemInfoOverlay5")
	};
	for (const FName WidgetName : RequiredWidgetNames)
	{
		TestNotNull(*FString::Printf(TEXT("Required designer variable exists: %s"), *WidgetName.ToString()),
			Tree->FindWidget(WidgetName));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConsumableQuickSlotHoldStateTest,
	"ArtisticSW.UI.ItemQuickSlot.HoldState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConsumableQuickSlotHoldStateTest::RunTest(const FString& Parameters)
{
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
	TestEqual(TEXT("Releasing slot 4 restores the still-held slot 3 highlight"),
		Player->GetPressedConsumableQuickSlotIndex(), 2);

	Player->EndConsumableQuickSlotInput(2);
	TestEqual(TEXT("Releasing the final held key clears the highlight"),
		Player->GetPressedConsumableQuickSlotIndex(), INDEX_NONE);
	TestEqual(TEXT("Every valid press and release broadcasts a visual state change"), ChangeCount, 4);

	Player->BeginConsumableQuickSlotInput(0);
	TestEqual(TEXT("Weapon slots cannot enter the consumable hold state"),
		Player->GetPressedConsumableQuickSlotIndex(), INDEX_NONE);

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}

#endif
