// Fill out your copyright notice in the Description page of Project Settings.


#include "Storage/StorageComponent.h"
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
		InitializeFromInitialItems();
	}
}

void UStorageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStorageComponent, SlotCount);
	DOREPLIFETIME(UStorageComponent, ColumnCount);
	DOREPLIFETIME(UStorageComponent, StorageSlots);
}

void UStorageComponent::ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

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

		if (Slot.ItemTag != ItemTag || Slot.Count >= MaxStack)
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

		if (!Slot.IsEmpty())
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
		CompactSlots();
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

	int32 AvailableCount = 0;
	for (const FInventorySlot& Slot : StorageSlots)
	{
		if (Slot.ItemTag == ItemTag)
		{
			AvailableCount += Slot.Count;
		}
	}

	if (AvailableCount < Amount)
	{
		return false;
	}

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

		const int32 MoveCount = FMath::Min(Slot.Count, Remaining);
		Slot.Count -= MoveCount;
		Remaining -= MoveCount;

		if (Slot.Count <= 0)
		{
			Slot.Clear();
		}
	}

	CompactSlots();
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

	if (!StorageSlots.IsValidIndex(SlotIndex))
	{
		return 0;
	}

	FInventorySlot& TargetSlot = StorageSlots[SlotIndex];
	const int32 MaxStack = FMath::Max(1, GetMaxStack(ItemTag));

	if (TargetSlot.IsEmpty())
	{
		const int32 AddedCount = FMath::Min(MaxStack, Amount);
		TargetSlot.ItemTag = ItemTag;
		TargetSlot.Count = AddedCount;

		CompactSlots();
		BroadcastStorageChanged();

		return AddedCount;
	}

	if (TargetSlot.ItemTag != ItemTag || TargetSlot.Count >= MaxStack)
	{
		return 0;
	}

	const int32 Space = MaxStack - TargetSlot.Count;
	const int32 AddedCount = FMath::Min(Space, Amount);
	TargetSlot.Count += AddedCount;

	CompactSlots();
	BroadcastStorageChanged();

	return AddedCount;
}

int32 UStorageComponent::TransferSlotToInventory(int32 SlotIndex, UInventoryComponent* TargetInventory)
{
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

	CompactSlots();
	BroadcastStorageChanged();

	return AddedCount;
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
	SlotCount = FMath::Max(1, SlotCount);
	ColumnCount = FMath::Max(1, ColumnCount);

	if (StorageSlots.Num() != GetSlotCount())
	{
		StorageSlots.SetNum(GetSlotCount());
		CompactSlots();
	}
}

void UStorageComponent::CompactSlots()
{
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

	StorageSlots = MoveTemp(CompactedSlots);
}

void UStorageComponent::BroadcastStorageChanged()
{
	OnStorageChanged.Broadcast();
}
