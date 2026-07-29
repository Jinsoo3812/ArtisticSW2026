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

void UStorageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStorageComponent, SlotCount);
	DOREPLIFETIME(UStorageComponent, ColumnCount);
	DOREPLIFETIME(UStorageComponent, StorageSlots);
}

void UStorageComponent::ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems)
{
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

	if (!StorageSlots.IsValidIndex(SlotIndex))
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
