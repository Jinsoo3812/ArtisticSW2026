// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageComponent.h"
#include "BaseGameplayTags.h"
#include "ItemSubsystem.h"
#include "Net/UnrealNetwork.h"

UStorageComponent::UStorageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UStorageComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!bConfiguredAtRuntime)
		{
			InitializeFromInitialItems();
		}
	}
}

void UStorageComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReturnAllReservedCursors();
	Super::EndPlay(Reason);
}

void UStorageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStorageComponent, SlotsPerTab);
	DOREPLIFETIME(UStorageComponent, SlotCount);
	DOREPLIFETIME(UStorageComponent, ColumnCount);
	DOREPLIFETIME(UStorageComponent, StorageSlots);
}

void UStorageComponent::ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems)
{
	if (!CursorReservations.IsEmpty()) return;
	bConfiguredAtRuntime = true;

	// storage 구성, slot의 개수, 열의 수, 아이템들 array를 전달하면 storage가 구성됨
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 슬롯 크기 조절 및 슬롯 초기화
	SlotCount = FMath::Max(1, InSlotCount);
	ColumnCount = FMath::Max(1, InColumnCount);

	StorageSlots.SetNum(GetSlotCount());
	for (FInventorySlot& Slot : StorageSlots)
	{
		Slot.Clear();
	}

	for (const FStorageItemEntry& Entry : InItems)
	{
		AddItem(Entry.ItemTag, Entry.Count);
	}

	CompactSlots();
	BroadcastStorageChanged();
}

int32 UStorageComponent::AddItem(const FGameplayTag& ItemTag, int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
	{
		return 0;
	}

	EnsureSlotArray();

	const int32 MaxStack = FMath::Max(1, GetMaxStack(ItemTag));
	int32 Remaining = Amount;
	int32 AddedCount = 0;

	for (FInventorySlot& Slot : StorageSlots)
	{
		if (Remaining <= 0)
		{
			break;
		}

		if (!CanStoreInSlot(static_cast<int32>(&Slot - StorageSlots.GetData()), ItemTag) || Slot.ItemTag != ItemTag || Slot.Count >= MaxStack)
		{
			continue;
		}

		const int32 Space = MaxStack - Slot.Count;
		const int32 MoveCount = FMath::Min(Space, Remaining);

		Slot.Count += MoveCount;
		Remaining -= MoveCount;
		AddedCount += MoveCount;
	}

	for (FInventorySlot& Slot : StorageSlots)
	{
		if (Remaining <= 0)
		{
			break;
		}

		if (!CanStoreInSlot(static_cast<int32>(&Slot - StorageSlots.GetData()), ItemTag) || !Slot.IsEmpty())
		{
			continue;
		}

		const int32 MoveCount = FMath::Min(MaxStack, Remaining);
		Slot.ItemTag = ItemTag;
		Slot.Count = MoveCount;

		Remaining -= MoveCount;
		AddedCount += MoveCount;
	}

	if (AddedCount > 0)
	{
		BroadcastStorageChanged();
	}

	return AddedCount;
}

bool UStorageComponent::RemoveItem(const FGameplayTag& ItemTag, int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
	{
		return false;
	}

	EnsureSlotArray();

	// 인자로 받은 tag로 동일한 아이템의 개수 count를 저장
	int32 AvailableCount = 0;
	for (const FInventorySlot& Slot : StorageSlots)
	{
		if (Slot.ItemTag == ItemTag)
		{
			AvailableCount += Slot.Count;
		}
	}

	// 가지고 있는 양보다 amount가 작으면 false 
	if (AvailableCount < Amount)
	{
		return false;
	}

	// amount 이상으로 가지고 있는 것을 확인, 
	// 지워야하는 남은 개수 remaining에 amount를 저장
	int32 Remaining = Amount;
	for (FInventorySlot& Slot : StorageSlots)
	{
		if (Remaining <= 0)
		{
			break;
		}

		if (Slot.ItemTag != ItemTag)
		{
			continue;
		}
		// 동일한 tag 아이템을 찾아서, 해당 칸에 있는 개수 전부, 빼야하는 개수 전부 중 더 작은 값 선택 (slot의 count가 reamining 보다 클 수 있으니까)
		const int32 MoveCount = FMath::Min(Slot.Count, Remaining);
		Slot.Count -= MoveCount;
		Remaining -= MoveCount;

		if (Slot.Count <= 0)
		{
			Slot.Clear();
		}
	}

	BroadcastStorageChanged();

	return true;
}

int32 UStorageComponent::AddItemToSlot(int32 SlotIndex, const FGameplayTag& ItemTag, int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
	{
		return 0;
	}

	EnsureSlotArray();

	if (!StorageSlots.IsValidIndex(SlotIndex) || !CanStoreInSlot(SlotIndex, ItemTag))
	{
		return 0;
	}

	FInventorySlot& TargetSlot = StorageSlots[SlotIndex];
	const int32 MaxStack = FMath::Max(1, GetMaxStack(ItemTag));

	if (TargetSlot.IsEmpty())
	{
		// 슬롯이 비어 있으면 추가
		const int32 AddedCount = FMath::Min(MaxStack, Amount);
		TargetSlot.ItemTag = ItemTag;
		TargetSlot.Count = AddedCount;

		BroadcastStorageChanged();

		return AddedCount;
	}

	if (TargetSlot.ItemTag != ItemTag || TargetSlot.Count >= MaxStack)
	{
		return 0;
	}
	// target slot이 비어있지 않을 때, 남은 공간 만큼 추가하고, 추가된 수량을 return
	const int32 Space = MaxStack - TargetSlot.Count;
	const int32 AddedCount = FMath::Min(Space, Amount);
	TargetSlot.Count += AddedCount;

	BroadcastStorageChanged();

	return AddedCount;
}

int32 UStorageComponent::TransferSlotToInventory(int32 SlotIndex, UInventoryComponent* TargetInventory)
{
	// storage에서 inventory로 이동 
	if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetInventory)
	{
		return 0;
	}

	EnsureSlotArray();

	if (!StorageSlots.IsValidIndex(SlotIndex) || StorageSlots[SlotIndex].IsEmpty())
	{
		return 0;
	}

	FInventorySlot& SourceSlot = StorageSlots[SlotIndex];
	const int32 AddedCount = TargetInventory->AddMaterial(SourceSlot.ItemTag, SourceSlot.Count);

	if (AddedCount <= 0)
	{
		return 0;
	}

	SourceSlot.Count -= AddedCount;
	if (SourceSlot.Count <= 0)
	{
		SourceSlot.Clear();
	}

	BroadcastStorageChanged();

	return AddedCount;
}

bool UStorageComponent::IsEmpty() const
{
	if (!CursorReservations.IsEmpty()) return false;
	for (const FInventorySlot& Slot : StorageSlots)
	{
		if (!Slot.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

int32 UStorageComponent::GetStorageRows() const
{
	return FMath::DivideAndRoundUp(GetSlotCount(), GetStorageColumns());
}

int32 UStorageComponent::GetMaxStack(const FGameplayTag& ItemTag) const
{
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
		{
			return Subsystem->GetMaxStack(ItemTag);
		}
	}

	return 99;
}

FGameplayTag UStorageComponent::GetItemRarityTag(const FGameplayTag& ItemTag) const
{
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
		{
			if (const FItemDefinition* ItemDefinition = Subsystem->GetItemDefinition(ItemTag))
			{
				return ItemDefinition->RarityTag;
			}
		}
	}

	return FGameplayTag();
}

int32 UStorageComponent::GetItemRarityRank(const FGameplayTag& ItemTag) const
{
	const FGameplayTag RarityTag = GetItemRarityTag(ItemTag);
	const int32 RarityRank = UItemData::GetRarityRank(RarityTag);

	return RarityRank > 0 ? RarityRank : UItemData::GetRarityRank(Item_Rarity_Common);
}

UTexture2D* UStorageComponent::GetItemIcon(const FGameplayTag& ItemTag) const
{
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
		{
			return Subsystem->GetIcon2D(ItemTag).LoadSynchronous();
		}
	}

	return nullptr;
}

FText UStorageComponent::GetItemName(const FGameplayTag& ItemTag) const
{
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
		{
			return Subsystem->GetItemName(ItemTag);
		}
	}

	return FText::FromString(ItemTag.ToString());
}

void UStorageComponent::OnRep_StorageContents()
{
	OnStorageChanged.Broadcast();
}

void UStorageComponent::InitializeFromInitialItems()
{
	SlotCount = FMath::Max(1, SlotCount);
	ColumnCount = FMath::Max(1, ColumnCount);

	StorageSlots.SetNum(GetSlotCount());
	for (FInventorySlot& Slot : StorageSlots)
	{
		Slot.Clear();
	}

	for (const FStorageItemEntry& Entry : InitialItems)
	{
		AddItem(Entry.ItemTag, Entry.Count);
	}

	CompactSlots();
	BroadcastStorageChanged();
}

void UStorageComponent::EnsureSlotArray()
{
	// slot의 수 확인
	SlotCount = FMath::Max(1, SlotCount);
	ColumnCount = FMath::Max(1, ColumnCount);

	// slot array의 칸 수와, slot의 수가 다르면, 일치하도록 설정
	if (StorageSlots.Num() != GetSlotCount())
	{
		StorageSlots.SetNum(GetSlotCount());
		CompactSlots();
	}
}

void UStorageComponent::CompactSlots()
{
	if (UsesInventoryTabs() || !CursorReservations.IsEmpty()) return;
	// 슬롯 압축
	// 아이템 array에 빈 공간이 있으면 앞으로 압축
	TArray<FInventorySlot> CompactedSlots;
	CompactedSlots.SetNum(GetSlotCount());

	int32 WriteIndex = 0;
	for (const FInventorySlot& Slot : StorageSlots)
	{
		if (Slot.IsEmpty() || !CompactedSlots.IsValidIndex(WriteIndex))
		{
			continue;
		}

		CompactedSlots[WriteIndex] = Slot;
		++WriteIndex;
	}

	CompactedSlots.StableSort([this](const FInventorySlot& Left, const FInventorySlot& Right)
	{
		if (Left.IsEmpty())
		{
			return false;
		}

		if (Right.IsEmpty())
		{
			return true;
		}

		return GetItemRarityRank(Left.ItemTag) < GetItemRarityRank(Right.ItemTag);
	});

	StorageSlots = MoveTemp(CompactedSlots);
}

void UStorageComponent::BroadcastStorageChanged()
{
	OnStorageChanged.Broadcast();
}

bool UStorageComponent::CanStoreInSlot(int32 Index, const FGameplayTag& ItemTag) const
{
	if (IsSlotReserved(Index)) return false;
	return !UsesInventoryTabs() || (Index >= 0 && Index / SlotsPerTab == static_cast<int32>(UInventoryComponent::ResolveItemTab(GetWorld(), ItemTag)));
}

bool UStorageComponent::ConfigureTabbedStorage(int32 InSlotsPerTab, const TArray<FInventorySlot>& SavedSlots, int32 SavedSlotsPerTab)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || InSlotsPerTab < 1 || InSlotsPerTab > 10000) return false;
	if (SavedSlotsPerTab > 0 && !CursorReservations.IsEmpty()) return false;
	const int32 OldCapacity = SavedSlotsPerTab > 0 ? SavedSlotsPerTab : SlotsPerTab;
	const TArray<FInventorySlot> Previous = SavedSlotsPerTab > 0 ? SavedSlots : StorageSlots;
	// Expansion never deletes previously saved slots, even after an editor default is reduced.
	const int32 NewCapacity = FMath::Max(InSlotsPerTab, OldCapacity);
	if (NewCapacity > 10000 || (SavedSlotsPerTab > 0 && SavedSlots.Num() != SavedSlotsPerTab * 4)) return false;
	TArray<FInventorySlot> NewSlots;
	NewSlots.SetNum(NewCapacity * 4);
	if (OldCapacity > 0)
	{
		for (int32 Tab = 0; Tab < 4; ++Tab)
			for (int32 Index = 0; Index < OldCapacity; ++Index)
				if (Previous.IsValidIndex(Tab * OldCapacity + Index))
					NewSlots[Tab * NewCapacity + Index] = Previous[Tab * OldCapacity + Index];
	}
	SlotsPerTab = NewCapacity;
	SlotCount = NewCapacity * 4;
	ColumnCount = 5;
	StorageSlots = MoveTemp(NewSlots);
	for (auto& Pair : CursorReservations)
	{
		if (OldCapacity > 0)
			Pair.Value.SlotIndex = (Pair.Value.SlotIndex / OldCapacity) * NewCapacity + Pair.Value.SlotIndex % OldCapacity;
		if (UInventoryComponent* Inventory = Pair.Key.Get())
		{
			Inventory->CursorItem.OriginalSlotIndex = Pair.Value.SlotIndex;
		}
	}
	TArray<TWeakObjectPtr<UInventoryComponent>> CursorOwners;
	CursorReservations.GetKeys(CursorOwners);
	for (const auto& Owner : CursorOwners)
		if (Owner.IsValid()) Owner->OnInventoryChanged.Broadcast();
	bConfiguredAtRuntime = true;
	BroadcastStorageChanged();
	return true;
}

bool UStorageComponent::IsSlotReserved(int32 Index) const
{
	for (const auto& Pair : CursorReservations)
		if (Pair.Value.SlotIndex == Index) return true;
	return false;
}

bool UStorageComponent::PickUpSlotToCursor(int32 SlotIndex, UInventoryComponent* Inventory)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory || !Inventory->GetOwner()
		|| !Inventory->GetOwner()->HasAuthority() || Inventory->CursorItem.IsValid()) return false;
	if (!StorageSlots.IsValidIndex(SlotIndex) || StorageSlots[SlotIndex].IsEmpty() || IsSlotReserved(SlotIndex)) return false;
	FCursorReservation Reservation;
	Reservation.SlotIndex = SlotIndex;
	Reservation.Item = StorageSlots[SlotIndex];
	CursorReservations.Add(Inventory, Reservation);
	Inventory->CursorItem.ItemTag = Reservation.Item.ItemTag;
	Inventory->CursorItem.Count = Reservation.Item.Count;
	Inventory->CursorItem.OriginalSlotIndex = SlotIndex;
	Inventory->CursorItem.OriginalTab = UInventoryComponent::ResolveItemTab(GetWorld(), Reservation.Item.ItemTag);
	Inventory->CursorItem.OriginalStorage = this;
	StorageSlots[SlotIndex].Clear();
	Inventory->OnInventoryChanged.AddUObject(this, &UStorageComponent::HandleCursorChanged);
	Inventory->OnInventoryChanged.Broadcast();
	BroadcastStorageChanged();
	return true;
}

void UStorageComponent::HandleCursorChanged()
{
	bool bChanged = false;
	for (auto It = CursorReservations.CreateIterator(); It; ++It)
	{
		UInventoryComponent* Inventory = It.Key().Get();
		if (!Inventory)
		{
			// EndPlay normally returns the item. Preserve it if an owner disappears unexpectedly.
			if (StorageSlots.IsValidIndex(It.Value().SlotIndex)) StorageSlots[It.Value().SlotIndex] = It.Value().Item;
			It.RemoveCurrent();
			bChanged = true;
		}
		else if (!Inventory->CursorItem.IsValid() || Inventory->CursorItem.OriginalStorage != this)
		{
			Inventory->OnInventoryChanged.RemoveAll(this);
			It.RemoveCurrent();
			bChanged = true;
		}
		else if (It.Value().Item.Count != Inventory->CursorItem.Count || It.Value().Item.ItemTag != Inventory->CursorItem.ItemTag)
		{
			It.Value().Item.ItemTag = Inventory->CursorItem.ItemTag;
			It.Value().Item.Count = Inventory->CursorItem.Count;
			bChanged = true;
		}
	}
	if (bChanged) BroadcastStorageChanged();
}

bool UStorageComponent::ReturnReservedCursor(UInventoryComponent* Inventory)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory) return false;
	const FCursorReservation* Reservation = CursorReservations.Find(Inventory);
	if (!Reservation || Inventory->CursorItem.OriginalStorage != this || !Inventory->CursorItem.IsValid()
		|| !StorageSlots.IsValidIndex(Reservation->SlotIndex) || !StorageSlots[Reservation->SlotIndex].IsEmpty()) return false;
	FInventorySlot& Slot = StorageSlots[Reservation->SlotIndex];
	Slot.ItemTag = Inventory->CursorItem.ItemTag;
	Slot.Count = Inventory->CursorItem.Count;
	CursorReservations.Remove(Inventory);
	Inventory->OnInventoryChanged.RemoveAll(this);
	Inventory->CursorItem.Clear();
	Inventory->OnInventoryChanged.Broadcast();
	BroadcastStorageChanged();
	return true;
}

void UStorageComponent::ReturnAllReservedCursors()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	HandleCursorChanged();
	TArray<TWeakObjectPtr<UInventoryComponent>> Inventories;
	CursorReservations.GetKeys(Inventories);
	for (const auto& Inventory : Inventories)
		if (Inventory.IsValid()) ReturnReservedCursor(Inventory.Get());
}

TArray<FInventorySlot> UStorageComponent::GetPersistentSlots() const
{
	TArray<FInventorySlot> Snapshot = StorageSlots;
	// A held stack remains owned by its source until the user commits a placement.
	// Preserve it in server saves while keeping the live slot empty and reserved.
	for (const auto& Pair : CursorReservations)
		if (Snapshot.IsValidIndex(Pair.Value.SlotIndex)) Snapshot[Pair.Value.SlotIndex] = Pair.Value.Item;
	return Snapshot;
}
