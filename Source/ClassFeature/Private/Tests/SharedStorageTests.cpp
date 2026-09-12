#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BaseGameplayTags.h"
#include "Storage/SharedStorageChest.h"
#include "Storage/SharedStorageSaveGame.h"
#include "Inventory/InventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "UI/InventoryPanelWidget.h"
#include "UI/StorageWindowWidget.h"
#include "UI/PlayerHUDWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "UObject/UnrealType.h"
#include "Blueprint/WidgetTree.h"
#include "Components/UniformGridPanel.h"
#include "Components/Button.h"
#include "Settings_Item.h"
#include "GameFramework/WorldSettings.h"
#include "BasePlayer.h"

namespace SharedStorageTests
{
struct FWorldScope
{
	UWorld* World;
	FWorldScope()
	{
		// Storage tests use real item definitions, but do not load unrelated crafting recipes.
		TGuardValue<TSoftObjectPtr<UDataTable>> RecipeScope(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	~FWorldScope() { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	UInventoryComponent* Inventory()
	{
		AActor* Owner = World->SpawnActor<AActor>();
		UInventoryComponent* Result = NewObject<UInventoryComponent>(Owner);
		Owner->AddInstanceComponent(Result);
		Result->RegisterComponent();
		return Result;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedStorageTransferTest, "ArtisticSW.SharedStorage.TransferAndExpansion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedStorageTransferTest::RunTest(const FString&)
{
	SharedStorageTests::FWorldScope Scope;
	AActor* Owner = Scope.World->SpawnActor<AActor>();
	UStorageComponent* Storage = NewObject<UStorageComponent>(Owner);
	Owner->AddInstanceComponent(Storage);
	Storage->RegisterComponent();
	TestTrue(TEXT("Configure 25 slots per tab"), Storage->ConfigureTabbedStorage(25));
	TestEqual(TEXT("Four tabs"), Storage->GetSlots().Num(), 100);
	const FGameplayTag Wood = Item_Id_Material_WeaponMaterial_Wood;
	const FGameplayTag Medicine = Item_Id_Consumables_Heal_Medicine;
	UInventoryComponent* First = Scope.Inventory();
	UInventoryComponent* Second = Scope.Inventory();
	TestEqual(TEXT("Seed inventory"), First->AddItem(Wood, 5), 5);
	TestEqual(TEXT("Deposit material stack"), First->TransferSlotToStorageInTab(EInventoryTab::Material, 0, Storage), 5);
	TestEqual(TEXT("Deposit debits player"), First->GetItemCount(Wood), 0);
	TestEqual(TEXT("Material uses its own tab"), Storage->GetSlots()[50].Count, 5);
	TestEqual(TEXT("Reject cross-tab cursor deposit"), Storage->AddItemToSlot(25, Wood, 1), 0);
	TestEqual(TEXT("Consumable uses independent capacity"), Storage->AddItem(Medicine, 2), 2);
	TestEqual(TEXT("Consumable tab receives items"), Storage->GetSlots()[25].Count, 2);
	TestEqual(TEXT("First viewer withdraws stack"), Storage->TransferSlotToInventory(50, First), 5);
	TestEqual(TEXT("Second viewer cannot duplicate it"), Storage->TransferSlotToInventory(50, Second), 0);
	TestEqual(TEXT("Second viewer got nothing"), Second->GetItemCount(Wood), 0);
	Storage->AddItemToSlot(74, Wood, 3);
	TestTrue(TEXT("Expand each tab to 50"), Storage->ConfigureTabbedStorage(50));
	TestEqual(TEXT("Slot offsets remapped without loss"), Storage->GetSlots()[124].Count, 3);
	TestEqual(TEXT("Other category preserved"), Storage->GetSlots()[50].Count, 2);
	Storage->ConfigureTabbedStorage(25);
	TestEqual(TEXT("Reducing defaults never truncates contents"), Storage->GetSlotsPerTab(), 50);
	TArray<FInventorySlot> Snapshot = Storage->GetSlots();
	TestFalse(TEXT("Malformed saved layout rejected"), Storage->ConfigureTabbedStorage(50, Snapshot, 25));
	TestEqual(TEXT("Rejected restore leaves live data"), Storage->GetSlots()[124].Count, 3);
	// Fill a single tab; other empty categories must not become overflow space.
	Storage->ConfigureTabbedStorage(50, TArray<FInventorySlot>(), 0);
	const int32 Max = Storage->GetMaxStack(Wood);
	for (int32 Index = 100; Index < 150; ++Index) Storage->AddItemToSlot(Index, Wood, Max);
	TestEqual(TEXT("Full material tab rejects overflow"), Storage->AddItem(Wood, 1), 0);
	First->AddItem(Wood, 1);
	const int32 Before = First->GetItemCount(Wood);
	TestEqual(TEXT("Failed transfer adds nothing"), First->TransferSlotToStorageInTab(EInventoryTab::Material, 0, Storage), 0);
	TestEqual(TEXT("Failed transfer preserves source"), First->GetItemCount(Wood), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedStoragePersistenceTest, "ArtisticSW.SharedStorage.PersistenceAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedStoragePersistenceTest::RunTest(const FString&)
{
	SharedStorageTests::FWorldScope Scope;
	const FGuid Id = FGuid::NewGuid();
	const FString Namespace = TEXT("Automation_") + FGuid::NewGuid().ToString();
	const FString SaveSlot = TEXT("SharedChest_") + FMD5::HashAnsiString(*(Namespace + TEXT("_") + Id.ToString()));
	auto Spawn = [&]()
	{
		ASharedStorageChest* Chest = Scope.World->SpawnActorDeferred<ASharedStorageChest>(ASharedStorageChest::StaticClass(), FTransform::Identity);
		Chest->PersistentChestId = Id;
		Chest->SaveNamespace = Namespace;
		Chest->FinishSpawning(FTransform::Identity);
		return Chest;
	};
	ASharedStorageChest* Chest = Spawn();
	TestFalse(TEXT("Fixed chest has no buoyancy"), Chest->IsPhysicsAndBuoyancyEnabled());
	TestFalse(TEXT("Fixed mesh does not simulate"), Chest->GetChestMesh()->IsSimulatingPhysics());
	TestEqual(TEXT("Initial capacity is 25"), Chest->GetStorageComponent()->GetSlotsPerTab(), 25);
	const FGameplayTag Wood = Item_Id_Material_WeaponMaterial_Wood;
	Chest->GetStorageComponent()->AddItem(Wood, 2);
	// Start an async snapshot, then modify while that snapshot may still be writing.
	Scope.World->GetTimerManager().Tick(1.1f);
	Chest->GetStorageComponent()->AddItem(Wood, 3);
	TestTrue(TEXT("Runtime expansion"), Chest->ExpandStorage(50));
	Chest->Destroy();
	TestTrue(TEXT("EndPlay flushed file"), UGameplayStatics::DoesSaveGameExist(SaveSlot, 0));
	Chest = Spawn();
	TestFalse(TEXT("Restored chest accessible"), Chest->IsLocked());
	TestEqual(TEXT("Saved expansion survives reload"), Chest->GetStorageComponent()->GetSlotsPerTab(), 50);
	if (TestTrue(TEXT("Restored slots exist"), Chest->GetStorageComponent()->GetSlots().IsValidIndex(100)))
		TestEqual(TEXT("Latest snapshot survives earlier async write"), Chest->GetStorageComponent()->GetSlots()[100].Count, 5);
	UInventoryComponent* CursorOwner = Scope.Inventory();
	TestTrue(TEXT("Pick up persisted stack"), Chest->GetStorageComponent()->PickUpSlotToCursor(100, CursorOwner));
	Chest->Destroy();
	TestFalse(TEXT("Chest teardown clears held cursor"), CursorOwner->GetCursorItem().IsValid());
	Chest = Spawn();
	if (TestTrue(TEXT("Held stack restored slots"), Chest->GetStorageComponent()->GetSlots().IsValidIndex(100)))
		TestEqual(TEXT("Held stack survives source teardown and reload"), Chest->GetStorageComponent()->GetSlots()[100].Count, 5);
	Chest->GetStorageComponent()->RemoveItem(Wood, 5);
	Chest->HandleEmptyDestroyTimeout();
	TestFalse(TEXT("Empty shared chest never auto-destroys"), Chest->IsActorBeingDestroyed());
	Chest->Destroy();
	Chest = Spawn();
	TestTrue(TEXT("Empty state persists across reload"), Chest->GetStorageComponent()->IsEmpty());
	Chest->Destroy();
	UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedStoragePanelAssetTest, "ArtisticSW.SharedStorage.InventoryPanelAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedStoragePanelAssetTest::RunTest(const FString&)
{
	UClass* PanelClass = LoadClass<UInventoryPanelWidget>(nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_Inventory/WBP_InventoryPanel.WBP_InventoryPanel_C"));
	if (!TestNotNull(TEXT("Existing inventory WBP is reusable"), PanelClass)) return false;
	SharedStorageTests::FWorldScope Scope;
	UInventoryPanelWidget* Panel = NewObject<UInventoryPanelWidget>(Scope.World, PanelClass);
	if (!TestNotNull(TEXT("Panel instance"), Panel)) return false;
	Panel->Initialize();
	TestNotNull(TEXT("InventoryGridPanel exists"), Cast<UUniformGridPanel>(Panel->WidgetTree->FindWidget(TEXT("InventoryGridPanel"))));
	for (const TCHAR* Name : { TEXT("ClueTabButton"), TEXT("ConsumableTabButton"), TEXT("MaterialTabButton"), TEXT("WeaponTabButton") })
		TestNotNull(Name, Cast<UButton>(Panel->WidgetTree->FindWidget(FName(Name))));
	ABasePlayer* Viewer = Scope.World->SpawnActor<ABasePlayer>();
	AStorageChest* Chest = Scope.World->SpawnActorDeferred<AStorageChest>(AStorageChest::StaticClass(), FTransform::Identity);
	Chest->SetPhysicsAndBuoyancyEnabled(false);
	Chest->FinishSpawning(FTransform::Identity);
	Chest->GetStorageComponent()->ConfigureTabbedStorage(25);
	const TSharedRef<SWidget> PanelSlate = Panel->TakeWidget();
	Panel->InitializeForStorage(Viewer, Chest);
	UUniformGridPanel* Grid = Cast<UUniformGridPanel>(Panel->WidgetTree->FindWidget(TEXT("InventoryGridPanel")));
	if (Grid) TestEqual(TEXT("Reused WBP builds 25 storage entries"), Grid->GetChildrenCount(), 25);
	const EInventoryTab OriginalTab = Viewer->GetInventoryComponent()->GetActiveTab();
	if (UButton* Button = Cast<UButton>(Panel->WidgetTree->FindWidget(TEXT("ConsumableTabButton")))) Button->OnClicked.Broadcast();
	TestEqual(TEXT("Storage tab does not change player inventory tab"), Viewer->GetInventoryComponent()->GetActiveTab(), OriginalTab);
	if (Grid) TestEqual(TEXT("Switching tab retains 25 entries"), Grid->GetChildrenCount(), 25);
	Panel->ForceLayoutPrepass();
	AddInfo(FString::Printf(TEXT("Reused inventory panel desired size: %.0f x %.0f"), Panel->GetDesiredSize().X, Panel->GetDesiredSize().Y));
	TestTrue(TEXT("Panel has nonzero layout size"), Panel->GetDesiredSize().X > 0 && Panel->GetDesiredSize().Y > 0);
	// Match HUD order: mount the native window first, then supply its chest and panel.
	// Testing the inventory WBP alone cannot detect a missing outer Slate root.
	UStorageWindowWidget* Window = NewObject<UStorageWindowWidget>(Scope.World);
	Window->Initialize();
	const TSharedRef<SWidget> WindowSlate = Window->TakeWidget();
	Window->InitializeStorage(Chest, Viewer);
	Window->UseInventoryPanel(PanelClass);
	Window->ForceLayoutPrepass();
	TestTrue(TEXT("Native chest root is attached to Slate"), Window->WidgetTree->RootWidget->GetCachedWidget().IsValid());
	TestTrue(TEXT("Mounted chest window includes inventory width"), Window->GetDesiredSize().X >= Panel->GetDesiredSize().X);
	TestTrue(TEXT("Mounted chest window includes inventory height"), Window->GetDesiredSize().Y >= Panel->GetDesiredSize().Y);
	Window->ReleaseSlateResources(true);
	Panel->ReleaseSlateResources(true);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedStorageDesignerLayoutTest, "ArtisticSW.SharedStorage.DesignerLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedStorageDesignerLayoutTest::RunTest(const FString&)
{
	SharedStorageTests::FWorldScope Scope;
	UClass* PanelClass = LoadClass<UInventoryPanelWidget>(nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_HUD/UI_Inventory/WBP_InventoryPanel.WBP_InventoryPanel_C"));
	if (!TestNotNull(TEXT("Inventory panel class"), PanelClass)) return false;
	auto Bind = [](UObject* Object, const TCHAR* Name, UObject* Value)
	{
		FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name)->SetObjectPropertyValue_InContainer(Object, Value);
	};
	UPlayerHUDWidget* HUD = NewObject<UPlayerHUDWidget>(Scope.World);
	HUD->Initialize();
	UCanvasPanel* Canvas = HUD->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvasPanel"));
	HUD->WidgetTree->RootWidget = Canvas;
	Bind(HUD, TEXT("RootCanvasPanel"), Canvas);
	UStorageWindowWidget* Window = NewObject<UStorageWindowWidget>(HUD->WidgetTree);
	Window->Initialize();
	UBorder* DesignerRoot = Window->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DesignerFrame"));
	Window->WidgetTree->RootWidget = DesignerRoot;
	UInventoryPanelWidget* Panel = NewObject<UInventoryPanelWidget>(Window->WidgetTree, PanelClass);
	Panel->Initialize();
	DesignerRoot->SetContent(Panel);
	Bind(Window, TEXT("SharedInventoryPanel"), Panel);
	Bind(HUD, TEXT("SharedStorageWindowWidget"), Window);
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Window);
	const FVector2D Position(820.0f, 90.0f);
	const FVector2D Size(620.0f, 760.0f);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
	const TSharedRef<SWidget> HUDSlate = HUD->TakeWidget();
	ABasePlayer* Viewer = Scope.World->SpawnActor<ABasePlayer>();
	ASharedStorageChest* Chest = Scope.World->SpawnActor<ASharedStorageChest>();
	TestTrue(TEXT("HUD uses the designer instance"), HUD->ShowStorageWindow(Chest, Viewer, nullptr) == Window);
	Window->UseInventoryPanel(PanelClass);
	TestTrue(TEXT("Designer root survives Slate construction"), Window->WidgetTree->RootWidget == DesignerRoot);
	TestTrue(TEXT("Designer inventory instance is reused"), DesignerRoot->GetContent() == Panel);
	TestEqual(TEXT("Designer position preserved"), Slot->GetPosition(), Position);
	TestEqual(TEXT("Designer size preserved"), Slot->GetSize(), Size);
	TestEqual(TEXT("Shared window opens"), Window->GetVisibility(), ESlateVisibility::Visible);
	UUniformGridPanel* Grid = Cast<UUniformGridPanel>(Panel->WidgetTree->FindWidget(TEXT("InventoryGridPanel")));
	if (TestNotNull(TEXT("Designer panel grid"), Grid)) TestEqual(TEXT("Designer panel shows chest slots"), Grid->GetChildrenCount(), 25);
	HUD->HideStorageWindow();
	TestEqual(TEXT("Designer window hides without removal"), Window->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("Designer window stays in its HUD slot"), Window->Slot == Slot);
	TestTrue(TEXT("Reopening reuses designer instance"), HUD->ShowStorageWindow(Chest, Viewer, nullptr) == Window);
	TestEqual(TEXT("Reopening preserves designer position"), Slot->GetPosition(), Position);
	HUD->HideStorageWindow();
	HUD->ReleaseSlateResources(true);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedStorageCursorTest, "ArtisticSW.SharedStorage.CursorPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedStorageCursorTest::RunTest(const FString&)
{
	SharedStorageTests::FWorldScope Scope;
	AActor* Owner = Scope.World->SpawnActor<AActor>();
	UStorageComponent* Storage = NewObject<UStorageComponent>(Owner);
	Owner->AddInstanceComponent(Storage);
	Storage->RegisterComponent();
	Storage->ConfigureTabbedStorage(25);
	UInventoryComponent* First = Scope.Inventory();
	UInventoryComponent* Second = Scope.Inventory();
	const FGameplayTag Wood = Item_Id_Material_WeaponMaterial_Wood;
	Storage->AddItem(Wood, 5);
	TestTrue(TEXT("Left click attaches stack to cursor"), Storage->PickUpSlotToCursor(50, First));
	TestEqual(TEXT("Held quantity"), First->GetCursorItem().Count, 5);
	TestEqual(TEXT("Pickup does not auto-deposit into inventory"), First->GetItemCount(Wood), 0);
	TestTrue(TEXT("Source is visually empty"), Storage->GetSlots()[50].IsEmpty());
	TestFalse(TEXT("Held loot does not count as an empty chest"), Storage->IsEmpty());
	TestFalse(TEXT("Second viewer cannot pick up same stack"), Storage->PickUpSlotToCursor(50, Second));
	TestEqual(TEXT("Original slot reserved for cancellation"), Storage->AddItemToSlot(50, Wood, 1), 0);
	TestEqual(TEXT("Save snapshot retains held stack"), Storage->GetPersistentSlots()[50].Count, 5);
	First->HandleLeftClickSlotInTab(EInventoryTab::Consumable, 0);
	TestEqual(TEXT("Wrong inventory tab preserves held stack"), First->GetCursorItem().Count, 5);
	const int32 MaxStack = First->GetMaxStack(Wood);
	First->AddItem(Wood, MaxStack - 2);
	First->HandleLeftClickSlotInTab(EInventoryTab::Material, 0);
	TestEqual(TEXT("Partial merge leaves remainder attached"), First->GetCursorItem().Count, 3);
	TestEqual(TEXT("Save tracks remainder only"), Storage->GetPersistentSlots()[50].Count, 3);
	Storage->ConfigureTabbedStorage(50);
	TestEqual(TEXT("Expansion remaps return slot"), First->GetCursorItem().OriginalSlotIndex, 100);
	First->HandleRightClickInventory();
	TestFalse(TEXT("Cancel clears cursor"), First->GetCursorItem().IsValid());
	TestEqual(TEXT("Cancel restores original source slot"), Storage->GetSlots()[100].Count, 3);
	Storage->PickUpSlotToCursor(100, First);
	TestEqual(TEXT("Place into another storage slot"), First->TransferCursorToStorageSlot(Storage, 101), 3);
	TestTrue(TEXT("Old reservation released"), Storage->GetPersistentSlots()[100].IsEmpty());
	Storage->PickUpSlotToCursor(101, First);
	First->HandleLeftClickSlotInTab(EInventoryTab::Material, 1);
	TestFalse(TEXT("Inventory placement clears cursor"), First->GetCursorItem().IsValid());
	TestEqual(TEXT("Inventory receives held stack"), First->GetSlots(EInventoryTab::Material)[1].Count, 3);
	TestTrue(TEXT("Committed move is no longer in source save"), Storage->GetPersistentSlots()[101].IsEmpty());
	Storage->AddItemToSlot(101, Wood, 1);
	Storage->PickUpSlotToCursor(101, Second);
	Second->GetOwner()->Destroy();
	TestEqual(TEXT("Player teardown restores source"), Storage->GetSlots()[101].Count, 1);
	Storage->PickUpSlotToCursor(101, First);
	TestEqual(TEXT("Click original slot puts stack back"), First->TransferCursorToStorageSlot(Storage, 101), 1);
	TestFalse(TEXT("Put-back clears cursor"), First->GetCursorItem().IsValid());
	return true;
}
#endif
