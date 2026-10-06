#include "Development/Voyage/SWVoyageTestFixtureSubsystem.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "BaseGameplayTags.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "MultiGameMode.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomReadyState.h"
#include "Room/SWRoomRuntimePaths.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"
#include "Ship.h"
#include "Storage/SharedStorageChest.h"
#include "Storage/StorageComponent.h"
#include "Upgrade/SharedShipUpgradeState.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWVoyageFixture, Log, All);

namespace SWVoyageFixture
{
const FGuid ChestId(0x53574649, 0x58545552, 0x4553544F, 0x52414745);
const TCHAR* ChestNamespace = TEXT("SWVoyageTestFixture");

bool IsAllowedWorld(const UWorld* World)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (!World || !World->IsGameWorld() || World->GetNetMode() != NM_DedicatedServer
		|| !FParse::Param(FCommandLine::Get(), TEXT("SWVoyageTestFixture"))) return false;
	const UGameInstance* Instance = World->GetGameInstance();
	const USWRoomProgressSubsystem* Room = Instance ? Instance->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	FString Root, Error;
	FString Diagnostics = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics")));
	FPaths::NormalizeDirectoryName(Diagnostics);
	return Room && Room->IsHostedRoom() && FSWRoomRuntimePaths::TryResolveRoot(Root, Error)
		&& FPaths::CollapseRelativeDirectories(Diagnostics) && FPaths::IsUnderDirectory(Root, Diagnostics);
#else
	return false;
#endif
}

bool IsAuthorizedIdleHost(ABasePlayerController* Requester)
{
	UWorld* World = Requester ? Requester->GetWorld() : nullptr;
	if (!IsAllowedWorld(World) || !Requester->HasAuthority()) return false;
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	USWVoyageResetSubsystem* Voyage = World->GetSubsystem<USWVoyageResetSubsystem>();
	ASWRoomReadyState* Ready = nullptr;
	for (TActorIterator<ASWRoomReadyState> It(World); It; ++It) { Ready = *It; break; }
	return Mode && Mode->IsRoomHostController(Requester) && Room && Room->GetActiveRoom()
		&& Room->IsDevelopmentTestSessionEnabled(World) && Voyage && !Voyage->IsGameplayBlocked()
		&& Ready && Ready->bWorldReady && Ready->RestoreGeneration == Room->GetRestoreGeneration()
		&& Ready->VoyageState.Phase == ESWVoyagePhase::Idle && !Mode->IsLevelRestartRequested()
		&& Mode->GetSessionLifePhase() == ESWSessionLifePhase::Playing;
}

FString InventoryValues(const TArray<FSWInventorySlotSnapshot>& Slots)
{
	TArray<FSWInventorySlotSnapshot> Ordered = Slots;
	Ordered.Sort([](const FSWInventorySlotSnapshot& A, const FSWInventorySlotSnapshot& B)
		{ return A.Tab == B.Tab ? A.SlotIndex < B.SlotIndex : A.Tab < B.Tab; });
	FString Values;
	for (const FSWInventorySlotSnapshot& Slot : Ordered)
		Values += FString::Printf(TEXT("%u:%d:%s:%d|"), Slot.Tab, Slot.SlotIndex, *Slot.ItemTag.ToString(), Slot.Count);
	return Values;
}
}

bool USWVoyageTestFixtureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return SWVoyageFixture::IsAllowedWorld(Cast<UWorld>(Outer)) && Super::ShouldCreateSubsystem(Outer);
}

void USWVoyageTestFixtureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<USWVoyageResetSubsystem>();
}

void USWVoyageTestFixtureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!SWVoyageFixture::IsAllowedWorld(&InWorld)) return;
	USWVoyageResetSubsystem* Voyage = InWorld.GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage || Voyage->GetGeneration() <= 0) return;

	// Do not introduce a second shared-storage system beside real authored content.
	bool bHasAuthoredStorage = false;
	for (TActorIterator<ASharedStorageChest> It(&InWorld); It; ++It)
		if (IsValid(*It)) { bHasAuthoredStorage = true; break; }
	if (!bHasAuthoredStorage)
	{
		ASharedStorageChest* Chest = FSWVoyageSpawn::SpawnDeferred<ASharedStorageChest>(&InWorld,
			ASharedStorageChest::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESWVoyageActorLifetime::SharedService, Voyage->GetGeneration());
		USWRoomSnapshotComponent* Snapshot = Chest ? Chest->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr;
		if (Chest && Snapshot && Chest->GetLevel() == InWorld.PersistentLevel)
		{
			Chest->PersistentChestId = SWVoyageFixture::ChestId;
			Chest->SaveNamespace = SWVoyageFixture::ChestNamespace;
			Chest->InitialSlotsPerTab = 25;
			Snapshot->PersistenceClass = ESWRoomPersistenceClass::Transient;
			Snapshot->SetRuntimeId(SWVoyageFixture::ChestId);
			if (USWVoyageSpawnLibrary::FinishVoyageActorSpawn(Chest, FTransform::Identity) == Chest) FixtureChest = Chest;
		}
		else if (Chest) Chest->Destroy();
	}
	if (!ASharedShipUpgradeState::Find(&InWorld))
	{
		ASharedShipUpgradeState* Upgrade = FSWVoyageSpawn::SpawnDeferred<ASharedShipUpgradeState>(&InWorld,
			ASharedShipUpgradeState::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESWVoyageActorLifetime::SharedService, Voyage->GetGeneration());
		if (Upgrade && Upgrade->GetLevel() == InWorld.PersistentLevel)
		{
			USWRoomSnapshotComponent* Snapshot = NewObject<USWRoomSnapshotComponent>(Upgrade, TEXT("FixtureSnapshotPolicy"));
			Snapshot->PersistenceClass = ESWRoomPersistenceClass::Transient;
			Upgrade->AddInstanceComponent(Snapshot);
			Snapshot->RegisterComponent();
			if (USWVoyageSpawnLibrary::FinishVoyageActorSpawn(Upgrade, FTransform::Identity) == Upgrade) FixtureUpgrade = Upgrade;
		}
		else if (Upgrade) Upgrade->Destroy();
	}
	UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=Created Storage=%d ExistingStorage=%d Upgrade=%d Generation=%d"),
		FixtureChest.IsValid(), bHasAuthoredStorage, FixtureUpgrade.IsValid(), Voyage->GetGeneration());
}

bool USWVoyageTestFixtureSubsystem::Seed(ABasePlayerController* Requester, FString& OutError)
{
	OutError = TEXT("Fixture seed requires the isolated Development host and unused fixture");
	if (!SWVoyageFixture::IsAuthorizedIdleHost(Requester) || bSeeded || !FixtureChest.IsValid()) return false;
	ASharedStorageChest* Chest = FixtureChest.Get();
	if (Chest->PersistentChestId != SWVoyageFixture::ChestId || Chest->SaveNamespace != SWVoyageFixture::ChestNamespace) return false;
	UWorld* World = GetWorld();
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	UItemSubsystem* Items = World->GetSubsystem<UItemSubsystem>();
	UItemData* Data = Items ? Items->GetItemDataAsset() : nullptr;
	UStorageComponent* Storage = Chest->GetStorageComponent();
	if (!Data || !Storage || !Storage->IsEmpty()) { OutError = TEXT("Fixture items unavailable or shared fixture already populated"); return false; }
	TArray<FGameplayTag> Tags;
	Data->ItemDefinitions.GetKeys(Tags);
	Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.ToString() < B.ToString(); });
	FGameplayTag TabTags[4];
	for (const FGameplayTag& Tag : Tags)
	{
		if (!Tag.IsValid()) continue;
		const uint8 Tab = static_cast<uint8>(UInventoryComponent::ResolveItemTab(World, Tag));
		if (Tab < 4 && !TabTags[Tab].IsValid() && Items->GetItemDefinition(Tag) && Items->GetMaxStack(Tag) > 0) TabTags[Tab] = Tag;
	}
	for (uint8 Tab = 0; Tab < 4; ++Tab)
		if (!TabTags[Tab].IsValid()) { OutError = FString::Printf(TEXT("Fixture has no verified item for tab %u"), Tab); return false; }

	struct FPersonalSeed
	{
		UInventoryComponent* Inventory = nullptr;
		int32 PlayerSlot = INDEX_NONE;
		TArray<FSWInventorySlotSnapshot> Slots;
	};
	TArray<FPersonalSeed> Personal;
	TSet<int32> SeenSlots;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
		ABasePlayer* Player = Controller ? Controller->GetLifeCharacter() : nullptr;
		const int32 PlayerSlot = Controller ? Mode->GetPlayerIndex(Controller) : INDEX_NONE;
		UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
		if (!Controller || !Controller->IsLifeCharacterAlive() || !Inventory || PlayerSlot < 0 || PlayerSlot > 1
			|| SeenSlots.Contains(PlayerSlot) || Inventory->GetCursorItem().IsValid())
		{ OutError = TEXT("Fixture personal inventory requires alive unique host/guest slots without cursor items"); return false; }
		SeenSlots.Add(PlayerSlot);
		FPersonalSeed& Seed = Personal.AddDefaulted_GetRef();
		Seed.Inventory = Inventory;
		Seed.PlayerSlot = PlayerSlot;
		Inventory->CaptureProgressSnapshot(Seed.Slots);
		for (uint8 Tab = 0; Tab < 4; ++Tab)
		{
			const TArray<FInventorySlot>& Page = Inventory->GetSlots(static_cast<EInventoryTab>(Tab));
			const int32 EmptyIndex = Page.IndexOfByPredicate([](const FInventorySlot& Slot)
				{ return !Slot.ItemTag.IsValid() && Slot.Count == 0; });
			if (EmptyIndex == INDEX_NONE) { OutError = FString::Printf(TEXT("Fixture personal slot %d tab %u has no empty slot"), PlayerSlot, Tab); return false; }
			FSWInventorySlotSnapshot& Empty = Seed.Slots.AddDefaulted_GetRef();
			Empty.Tab = Tab;
			Empty.SlotIndex = EmptyIndex;
			Empty.ItemTag = TabTags[Tab];
			Empty.Count = FMath::Min(Items->GetMaxStack(TabTags[Tab]), 2 + PlayerSlot * 5 + Tab);
		}
	}
	if (!SeenSlots.Contains(0)) return false;
	LogObservation(TEXT("SeedBefore"));
	if (!Chest->ExpandStorage(30)) { OutError = TEXT("Fixture capacity expansion failed"); return false; }
	for (uint8 Tab = 0; Tab < 4; ++Tab)
	{
		const int32 Count = FMath::Min(Items->GetMaxStack(TabTags[Tab]), 12 + Tab);
		if (Storage->AddItemToSlot(Storage->GetTabStart(static_cast<EInventoryTab>(Tab)), TabTags[Tab], Count) != Count)
		{ OutError = TEXT("Fixture shared slot seed failed"); LogObservation(TEXT("SeedPartial")); return false; }
	}
	for (FPersonalSeed& Seed : Personal)
	{
		Seed.Inventory->RestoreProgressSnapshot(Seed.Slots);
		TArray<FSWInventorySlotSnapshot> Actual;
		Seed.Inventory->CaptureProgressSnapshot(Actual);
		if (SWVoyageFixture::InventoryValues(Seed.Slots) != SWVoyageFixture::InventoryValues(Actual))
		{ OutError = TEXT("Fixture personal seed verification failed"); LogObservation(TEXT("SeedPartial")); return false; }
	}

	// Only the explicitly created fixture singleton may be seeded. Real content
	// upgrades and material economics are outside this storage-policy fixture.
	bool bUpgradeSeeded = false;
	ASharedShipUpgradeState* UpgradeState = FixtureUpgrade.Get();
	if (UpgradeState != ASharedShipUpgradeState::Find(World)) UpgradeState = nullptr;
	UShipUpgradeComponent* Upgrade = UpgradeState ? UpgradeState->GetUpgradeComponent() : nullptr;
	UShipUpgradeTreeDataAsset* Tree = Upgrade ? Upgrade->UpgradeTree.Get() : nullptr;
	TArray<FText> TreeErrors;
	if (Tree && Tree->ValidateTree(TreeErrors) && Upgrade->GetActiveNodeIds().IsEmpty())
	{
		TArray<FName> Roots;
		for (const FShipUpgradeNodeDefinition& Node : Tree->Nodes)
			if (!Node.NodeId.IsNone() && Node.PrerequisiteNodeIds.IsEmpty()) Roots.Add(Node.NodeId);
		Roots.Sort(FNameLexicalLess());
		if (!Roots.IsEmpty())
		{
			Upgrade->RestoreActiveNodeIds({ Roots[0] });
			bUpgradeSeeded = Upgrade->GetActiveNodeIds().Num() == 1 && Upgrade->GetActiveNodeIds()[0] == Roots[0];
		}
	}
	bSeeded = true;
	LogObservation(TEXT("SeedAfter"));
	UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=Seeded Players=%d Capacity=%d UpgradeSeeded=%d"),
		Personal.Num(), Storage->GetSlotsPerTab(), bUpgradeSeeded);
	OutError = bUpgradeSeeded ? TEXT("Fixture storage/personal tabs and valid upgrade node seeded")
		: TEXT("Fixture storage/personal tabs seeded; upgrade seeding unsupported or existing content retained");
	return true;
}

bool USWVoyageTestFixtureSubsystem::Observe(ABasePlayerController* Requester, FString& OutError) const
{
	if (!SWVoyageFixture::IsAuthorizedIdleHost(Requester)) { OutError = TEXT("Fixture observation requires the isolated idle Development host"); return false; }
	LogObservation(TEXT("Observe"));
	OutError = TEXT("Fixture values logged");
	return true;
}

void USWVoyageTestFixtureSubsystem::ResumeVoyage_Implementation(const FSWVoyageResetContext& Context)
{
	LogObservation(TEXT("Release"));
}

void USWVoyageTestFixtureSubsystem::LogObservation(const TCHAR* Event) const
{
	const UWorld* World = GetWorld();
	if (!SWVoyageFixture::IsAllowedWorld(World)) return;
	const AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	const USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
		const ABasePlayer* Player = Controller ? Controller->GetLifeCharacter() : nullptr;
		const UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
		if (!Inventory || !Mode) continue;
		TArray<FSWInventorySlotSnapshot> Slots;
		Inventory->CaptureProgressSnapshot(Slots);
		FString Capacities;
		for (uint8 Tab = 0; Tab < 4; ++Tab)
			Capacities += FString::Printf(TEXT("%u:%d|"), Tab, Inventory->GetSlotCount(static_cast<EInventoryTab>(Tab)));
		const FString Values = Capacities + SWVoyageFixture::InventoryValues(Slots);
		UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=%s Kind=Personal PlayerSlot=%d Generation=%d Hash=%s Slots=%d"),
			Event, Mode->GetPlayerIndex(Controller), Room->GetRestoreGeneration(), *FMD5::HashAnsiString(*Values), Slots.Num());
		UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=%s Kind=PersonalCapacity PlayerSlot=%d Values=%s"),
			Event, Mode->GetPlayerIndex(Controller), *Capacities);
		for (const FSWInventorySlotSnapshot& Slot : Slots)
			if (Slot.ItemTag.IsValid() || Slot.Count != 0)
				UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=%s Kind=PersonalValue PlayerSlot=%d Tab=%u Slot=%d Tag=%s Count=%d"),
					Event, Mode->GetPlayerIndex(Controller), Slot.Tab, Slot.SlotIndex, *Slot.ItemTag.ToString(), Slot.Count);
	}
	if (const ASharedStorageChest* Chest = FixtureChest.Get())
	{
		const UStorageComponent* Storage = Chest->GetStorageComponent();
		if (Storage)
		{
			const TArray<FInventorySlot> Slots = Storage->GetPersistentSlots();
			FString Values = FString::Printf(TEXT("Capacity=%d|"), Storage->GetSlotsPerTab());
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
				Values += FString::Printf(TEXT("%d:%s:%d|"), Index, *Slots[Index].ItemTag.ToString(), Slots[Index].Count);
			UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=%s Kind=Shared Generation=%d ChestId=%s Namespace=%s Capacity=%d Hash=%s Slots=%d"),
				Event, Room->GetRestoreGeneration(), *Chest->PersistentChestId.ToString(), *Chest->SaveNamespace,
				Storage->GetSlotsPerTab(), *FMD5::HashAnsiString(*Values), Slots.Num());
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
				if (Slots[Index].ItemTag.IsValid() || Slots[Index].Count != 0)
					UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=%s Kind=SharedValue Slot=%d Tag=%s Count=%d"),
						Event, Index, *Slots[Index].ItemTag.ToString(), Slots[Index].Count);
		}
	}
	const ASharedShipUpgradeState* State = ASharedShipUpgradeState::Find(World);
	const UShipUpgradeComponent* Upgrade = State ? State->GetUpgradeComponent() : nullptr;
	if (Upgrade)
	{
		TArray<FName> Nodes = Upgrade->GetActiveNodeIds();
		Nodes.Sort(FNameLexicalLess());
		FString Values;
		for (FName Node : Nodes) Values += Node.ToString() + TEXT("|");
		UE_LOG(LogSWVoyageFixture, Display, TEXT("Event=%s Kind=Upgrades Generation=%d Hash=%s Count=%d Values=%s"),
			Event, Room->GetRestoreGeneration(), *FMD5::HashAnsiString(*Values), Nodes.Num(), *Values);
	}
}
