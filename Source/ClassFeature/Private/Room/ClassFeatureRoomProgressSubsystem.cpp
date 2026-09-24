#include "Room/ClassFeatureRoomProgressSubsystem.h"

#include "BasePlayer.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MultiGameMode.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSaveGame.h"
#include "Storage/SharedStorageChest.h"
#include "Storage/StorageComponent.h"
#include "Inventory/InventoryComponent.h"
#include "StorySubsystem.h"
#include "StorySaveGame.h"
#include "Upgrade/SharedShipUpgradeState.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Containers/Ticker.h"

namespace
{
USWRoomProgressSubsystem* GetRoom(UGameInstance* GameInstance)
{
	USWRoomProgressSubsystem* Room = GameInstance ? GameInstance->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	return Room && Room->IsHostedRoom() && !Room->HasStartupError() ? Room : nullptr;
}

FString StorageKey(const FGuid& Id, const FString& Namespace)
{
	return Id.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("|") + Namespace;
}
}

void UClassFeatureRoomProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UClassFeatureRoomProgressSubsystem::HandlePostLoadMap);
}

void UClassFeatureRoomProgressSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (RestoreTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RestoreTickerHandle);
	Super::Deinitialize();
}

void UClassFeatureRoomProgressSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (!GetRoom(GetGameInstance()) || !World || World->GetGameInstance() != GetGameInstance()) return;
	PendingWorld = World;
	RestoreDeadline = FPlatformTime::Seconds() + 10.0;
	bReturning = false;
	if (RestoreTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RestoreTickerHandle);
	RestoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UClassFeatureRoomProgressSubsystem::TickRestore), 0.0f);
}

bool UClassFeatureRoomProgressSubsystem::TickRestore(float DeltaTime)
{
	UWorld* World = PendingWorld.Get();
	if (!World || World->GetGameInstance() != GetGameInstance()) return false;
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	if (!Mode || !World->HasBegunPlay())
	{
		if (FPlatformTime::Seconds() < RestoreDeadline) return true;
		UE_LOG(LogTemp, Error, TEXT("Hosted room world did not become ready"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	if (!RestoreSharedWorld(World))
	{
		if (FPlatformTime::Seconds() < RestoreDeadline) return true;
		UE_LOG(LogTemp, Error, TEXT("Hosted room shared progress restore failed"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	Mode->MarkHostedRoomWorldReady();
	Mode->OnGameOverRequested.AddDynamic(this, &UClassFeatureRoomProgressSubsystem::HandleGameOverRestart);
	PendingWorld.Reset();
	RestoreTickerHandle.Reset();
	return false;
}

void UClassFeatureRoomProgressSubsystem::HandleGameOverRestart()
{
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		if (!CaptureSharedWorld(World)) UE_LOG(LogTemp, Error, TEXT("Hosted room shared progress capture failed during game-over restart"));
}

bool UClassFeatureRoomProgressSubsystem::RestoreSharedWorld(UWorld* World)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
	if (!World || !Save) return false;
	const FSWRoomSharedProgress& Shared = Save->SharedProgress;
	ASharedShipUpgradeState* Ship = ASharedShipUpgradeState::Find(World);
	if (!Ship && !Shared.ShipUpgradeNodeIds.IsEmpty() && FPlatformTime::Seconds() < RestoreDeadline) return false;
	if (UStorySubsystem* Story = GetGameInstance()->GetSubsystem<UStorySubsystem>())
	{
		UStorySaveGame* StoryData = NewObject<UStorySaveGame>(this);
		StoryData->StoryDefinitionId = Shared.StoryDefinitionId;
		StoryData->Facts = Shared.Facts;
		StoryData->AppliedActionKeys = Shared.AppliedActionKeys;
		for (const FSWRoomCounterProgress& Counter : Shared.Counters)
		{
			FStoryCounterValue& Value = StoryData->Counters.AddDefaulted_GetRef();
			Value.CounterTag = Counter.Tag;
			Value.Value = Counter.Value;
		}
		if (Shared.StoryDefinitionId.IsValid() && !Story->ApplyRoomProgress(StoryData)) return false;
	}
	if (Ship)
		if (UShipUpgradeComponent* Upgrade = Ship->GetUpgradeComponent())
			Upgrade->RestoreActiveNodeIds(Shared.ShipUpgradeNodeIds);
	TSet<FString> Keys;
	for (TActorIterator<ASharedStorageChest> It(World); It; ++It)
	{
		ASharedStorageChest* Chest = *It;
		const FString Key = StorageKey(Chest->PersistentChestId, Chest->SaveNamespace);
		if (!Chest->PersistentChestId.IsValid() || Keys.Contains(Key)) return false;
		Keys.Add(Key);
		const FSWRoomStorageProgress* Stored = Shared.Storage.FindByPredicate([&Key](const FSWRoomStorageProgress& Entry)
		{
			return StorageKey(Entry.ChestId, Entry.SaveNamespace) == Key;
		});
		if (!Stored) continue;
		TArray<FInventorySlot> Slots;
		for (const FSWRoomStorageSlot& Entry : Stored->Slots)
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemTag = Entry.ItemTag;
			Slot.Count = Entry.Count;
		}
		if (!Chest->GetStorageComponent()->ConfigureTabbedStorage(Chest->InitialSlotsPerTab, Slots, Stored->SlotsPerTab)) return false;
	}
	return true;
}

bool UClassFeatureRoomProgressSubsystem::CaptureSharedWorld(UWorld* World)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
	if (!World || !Save) return false;
	FSWRoomSharedProgress& Shared = Save->SharedProgress;
	if (UStorySubsystem* Story = GetGameInstance()->GetSubsystem<UStorySubsystem>())
	{
		UStorySaveGame* StoryData = Story->BuildRoomProgress();
		if (!StoryData) return false;
		Shared.StoryDefinitionId = StoryData->StoryDefinitionId;
		Shared.Facts = StoryData->Facts;
		Shared.Counters.Reset();
		for (const FStoryCounterValue& Counter : StoryData->Counters)
		{
			FSWRoomCounterProgress& Value = Shared.Counters.AddDefaulted_GetRef();
			Value.Tag = Counter.CounterTag;
			Value.Value = Counter.Value;
		}
		Shared.AppliedActionKeys = StoryData->AppliedActionKeys;
	}
	if (ASharedShipUpgradeState* Ship = ASharedShipUpgradeState::Find(World))
		if (const UShipUpgradeComponent* Upgrade = Ship->GetUpgradeComponent()) Shared.ShipUpgradeNodeIds = Upgrade->GetActiveNodeIds();
	TSet<FString> Keys;
	for (TActorIterator<ASharedStorageChest> It(World); It; ++It)
	{
		ASharedStorageChest* Chest = *It;
		const FString Key = StorageKey(Chest->PersistentChestId, Chest->SaveNamespace);
		if (!Chest->PersistentChestId.IsValid() || Keys.Contains(Key)) return false;
		Keys.Add(Key);
		UStorageComponent* Storage = Chest->GetStorageComponent();
		Storage->ReturnAllReservedCursors();
		FSWRoomStorageProgress* Entry = Shared.Storage.FindByPredicate([&Key](const FSWRoomStorageProgress& Record)
		{
			return StorageKey(Record.ChestId, Record.SaveNamespace) == Key;
		});
		if (!Entry) Entry = &Shared.Storage.AddDefaulted_GetRef();
		Entry->ChestId = Chest->PersistentChestId;
		Entry->SaveNamespace = Chest->SaveNamespace;
		Entry->SlotsPerTab = Storage->GetSlotsPerTab();
		Entry->Slots.Reset();
		for (const FInventorySlot& Slot : Storage->GetPersistentSlots())
		{
			FSWRoomStorageSlot& SavedSlot = Entry->Slots.AddDefaulted_GetRef();
			SavedSlot.ItemTag = Slot.ItemTag;
			SavedSlot.Count = Slot.Count;
		}
	}
	Shared.ShipUpgradeNodeIds.Sort(FNameLexicalLess());
	Shared.Storage.Sort([](const FSWRoomStorageProgress& A, const FSWRoomStorageProgress& B)
	{
		return StorageKey(A.ChestId, A.SaveNamespace) < StorageKey(B.ChestId, B.SaveNamespace);
	});
	return true;
}

void UClassFeatureRoomProgressSubsystem::RestorePlayer(ABasePlayer* Player)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
	AMultiGameMode* Mode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Save || !Mode || !Player->GetController()) return;
	if (Mode->GetPlayerIndex(Player->GetController()) == 0) Player->RestoreRoomProgress(Save->HostProgress);
	else if (const APlayerState* State = Player->GetPlayerState())
	{
		const FString Name = State->GetPlayerName();
		if (const FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; }))
			Player->RestoreRoomProgress(Guest->Progress);
		else Player->RestoreRoomProgress(FSWRoomPlayerProgress());
	}
}

void UClassFeatureRoomProgressSubsystem::CapturePlayer(ABasePlayer* Player)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
	AMultiGameMode* Mode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Save || !Mode) return;
	AController* Controller = Player->GetController();
	if (!Controller && Player->GetPlayerState()) Controller = Player->GetPlayerState()->GetOwningController();
	if (!Controller) return;
	if (Mode->GetPlayerIndex(Controller) == 0) Player->CaptureRoomProgress(Save->HostProgress);
	else if (const APlayerState* State = Player->GetPlayerState())
	{
		const FString Name = State->GetPlayerName();
		FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; });
		if (!Guest) Guest = &Save->Guests.AddDefaulted_GetRef();
		Guest->DisplayName = Name;
		Player->CaptureRoomProgress(Guest->Progress);
		Save->Guests.Sort([](const FSWRoomGuestProgress& A, const FSWRoomGuestProgress& B) { return A.DisplayName < B.DisplayName; });
	}
}

bool UClassFeatureRoomProgressSubsystem::TryReturn(UWorld* World, ABasePlayer* Requester)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Room || !Mode || !Requester || bReturning) return false;
	for (TActorIterator<ABasePlayer> It(World); It; ++It)
	{
		if (UInventoryComponent* Inventory = It->GetInventoryComponent()) Inventory->ReturnCursorToOriginalSlot();
		CapturePlayer(*It);
	}
	if (!CaptureSharedWorld(World) || !Room->WriteCheckpoint())
	{
		if (APlayerController* Controller = Cast<APlayerController>(Requester->GetController())) Controller->ClientMessage(TEXT("귀환 저장 실패"));
		return false;
	}
	bReturning = true;
	Mode->RequestHostedRoomReturnTravel();
	return true;
}
