// Fill out your copyright notice in the Description page of Project Settings.


#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "ItemData.h"
#include "ItemSubsystem.h"
#include "Engine/Engine.h"
#include "Storage/StorageComponent.h"
#include "BaseGameplayTags.h"

namespace
{
	int32 CountItemInSlots(const TArray<FInventorySlot>& Slots, const FGameplayTag& ItemTag)
	{
		int32 Total = 0;
		for (const FInventorySlot& Slot : Slots)
		{
			if (Slot.ItemTag == ItemTag)
			{
				Total += Slot.Count;
			}
		}
		return Total;
	}

	bool RemoveFromSlots(TArray<FInventorySlot>& Slots, const FGameplayTag& ItemTag, int32 Amount)
	{
		if (!ItemTag.IsValid() || Amount <= 0 || CountItemInSlots(Slots, ItemTag) < Amount)
		{
			return false;
		}
		int32 Remaining = Amount;
		for (FInventorySlot& Slot : Slots)
		{
			if (Slot.ItemTag != ItemTag)
			{
				continue;
			}
			const int32 Removed = FMath::Min(Remaining, Slot.Count);
			Slot.Count -= Removed;
			Remaining -= Removed;
			if (Slot.Count <= 0)
			{
				Slot.Clear();
			}
			if (Remaining == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool AddToSlots(TArray<FInventorySlot>& Slots, const FGameplayTag& ItemTag, int32 Amount, int32 MaxStack)
	{
		if (!ItemTag.IsValid() || Amount <= 0 || MaxStack <= 0)
		{
			return false;
		}
		int32 Remaining = Amount;
		for (FInventorySlot& Slot : Slots)
		{
			if (Remaining == 0)
			{
				break;
			}
			if (Slot.ItemTag == ItemTag && Slot.Count < MaxStack)
			{
				const int32 Added = FMath::Min(Remaining, MaxStack - Slot.Count);
				Slot.Count += Added;
				Remaining -= Added;
			}
		}
		for (FInventorySlot& Slot : Slots)
		{
			if (Remaining == 0)
			{
				break;
			}
			if (Slot.IsEmpty())
			{
				const int32 Added = FMath::Min(Remaining, MaxStack);
				Slot.ItemTag = ItemTag;
				Slot.Count = Added;
				Remaining -= Added;
			}
		}
		return Remaining == 0;
	}

	FInventoryTabPage* FindPageInArray(TArray<FInventoryTabPage>& Pages, EInventoryTab Tab)
	{
		return Pages.FindByPredicate([Tab](const FInventoryTabPage& Page)
		{
			return Page.Tab == Tab;
		});
	}
}

// Sets default values for this component's properties
UInventoryComponent::UInventoryComponent()
{
	SetIsReplicatedByDefault(true);

	InventoryTabConfigs =
	{
		{ EInventoryTab::Clue, 25 },
		{ EInventoryTab::Consumable, 25 },
		{ EInventoryTab::Material, 25 },
		{ EInventoryTab::Weapon, 25 }
	};
}

void UInventoryComponent::BeginPlay()
{
    Super::BeginPlay();
	OnInventoryChanged.AddUObject(this, &UInventoryComponent::BroadcastShipUpgradeInventoryChanged);
    //서버에서만 인벤토리 array 크기 초기화
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        InitializeInventoryPages();
    }
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UInventoryComponent, InventorySlots);
    DOREPLIFETIME(UInventoryComponent, InventoryPages);
    DOREPLIFETIME(UInventoryComponent, CursorItem);
}

void UInventoryComponent::OnRep_InventoryContents()
{
    if (const FInventoryTabPage* Page = FindPage(ActiveTab))
    {
        InventorySlots = Page->Slots;
    }
    OnInventoryChanged.Broadcast();
}

void UInventoryComponent::InitializeInventoryPages()
{
    if (InventoryColumns <= 0)
    {
        InventoryColumns = 5;
    }

    if (InventoryTabConfigs.Num() == 0)
    {
        InventoryTabConfigs =
        {
            { EInventoryTab::Clue, 25 },
            { EInventoryTab::Consumable, 25 },
            { EInventoryTab::Material, 25 },
            { EInventoryTab::Weapon, 25 }
        };
    }

    for (const FInventoryTabConfig& Config : InventoryTabConfigs)
    {
        FInventoryTabPage* Page = FindMutablePage(Config.Tab);
        if (!Page)
        {
            FInventoryTabPage NewPage;
            NewPage.Tab = Config.Tab;
            InventoryPages.Add(NewPage);
            Page = &InventoryPages.Last();
        }

        Page->Slots.SetNum(FMath::Max(0, Config.SlotCount));
    }

    if (FInventoryTabPage* ActivePage = FindMutablePage(ActiveTab))
    {
        InventorySlots = ActivePage->Slots;
    }
}

FInventoryTabPage* UInventoryComponent::FindMutablePage(EInventoryTab Tab)
{
    return InventoryPages.FindByPredicate([Tab](const FInventoryTabPage& Page)
    {
        return Page.Tab == Tab;
    });
}

const FInventoryTabPage* UInventoryComponent::FindPage(EInventoryTab Tab) const
{
    return InventoryPages.FindByPredicate([Tab](const FInventoryTabPage& Page)
    {
        return Page.Tab == Tab;
    });
}

const TArray<FInventorySlot>& UInventoryComponent::GetSlots(EInventoryTab Tab) const
{
    if (const FInventoryTabPage* Page = FindPage(Tab))
    {
        return Page->Slots;
    }

    static const TArray<FInventorySlot> EmptySlots;
    return EmptySlots;
}

void UInventoryComponent::CaptureProgressSnapshot(TArray<FSWInventorySlotSnapshot>& OutSlots) const
{
	OutSlots.Reset();
	for (const FInventoryTabPage& Page : InventoryPages)
	{
		for (int32 SlotIndex = 0; SlotIndex < Page.Slots.Num(); ++SlotIndex)
		{
			const FInventorySlot& Slot = Page.Slots[SlotIndex];
			if (Slot.IsEmpty()) continue;
			FSWInventorySlotSnapshot& Saved = OutSlots.AddDefaulted_GetRef();
			Saved.Tab = static_cast<uint8>(Page.Tab);
			Saved.SlotIndex = SlotIndex;
			Saved.ItemTag = Slot.ItemTag;
			Saved.Count = Slot.Count;
		}
	}
}

void UInventoryComponent::RestoreProgressSnapshot(const TArray<FSWInventorySlotSnapshot>& InSlots)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	InitializeInventoryPages();
	for (FInventoryTabPage& Page : InventoryPages)
	{
		for (FInventorySlot& Slot : Page.Slots) Slot.Clear();
	}
	CursorItem.Clear();
	for (const FSWInventorySlotSnapshot& Saved : InSlots)
	{
		FInventoryTabPage* Page = FindMutablePage(static_cast<EInventoryTab>(Saved.Tab));
		if (!Page || !Page->Slots.IsValidIndex(Saved.SlotIndex) || !Saved.ItemTag.IsValid() || Saved.Count <= 0) continue;
		Page->Slots[Saved.SlotIndex].ItemTag = Saved.ItemTag;
		Page->Slots[Saved.SlotIndex].Count = Saved.Count;
	}
	OnRep_InventoryContents();
}

int32 UInventoryComponent::GetSlotCount(EInventoryTab Tab) const
{
    if (const FInventoryTabPage* Page = FindPage(Tab))
    {
        return Page->Slots.Num();
    }

    if (const FInventoryTabConfig* Config = InventoryTabConfigs.FindByPredicate([Tab](const FInventoryTabConfig& Candidate)
    {
        return Candidate.Tab == Tab;
    }))
    {
        return FMath::Max(0, Config->SlotCount);
    }

    return 0;
}

int32 UInventoryComponent::GetInventoryRows(EInventoryTab Tab) const
{
    const int32 Columns = FMath::Max(1, InventoryColumns);
    return FMath::DivideAndRoundUp(GetSlotCount(Tab), Columns);
}

void UInventoryComponent::SetActiveTab(EInventoryTab NewTab)
{
    ActiveTab = NewTab;

    if (const FInventoryTabPage* Page = FindPage(ActiveTab))
    {
        InventorySlots = Page->Slots;
    }
    else
    {
        InventorySlots.Reset();
    }

    OnInventoryChanged.Broadcast();
}

bool UInventoryComponent::CanSlotAcceptItem(EInventoryTab Tab, const FGameplayTag& ItemTag) const
{
    return ItemTag.IsValid() && GetInventoryTabForItem(ItemTag) == Tab;
}

int32 UInventoryComponent::AddMaterial(const FGameplayTag& ItemTag, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
    {
        return 0;
    }

    InitializeInventoryPages();

    const EInventoryTab ItemTab = GetInventoryTabForItem(ItemTag);
    FInventoryTabPage* Page = FindMutablePage(ItemTab);
    if (!Page)
    {
        return 0;
    }

    TArray<FInventorySlot>& TargetSlots = Page->Slots;

    const int32 MaxStack = GetMaxStack(ItemTag);

    int32 Remaining = Amount;
    int32 AddedCount = 0;
    
    // 1. 같은 아이템 슬롯에 먼저 가능한 만큼 누적
    for (FInventorySlot& Slot : TargetSlots)
    {
        if (Remaining <= 0) break;
     
        if (Slot.ItemTag != ItemTag) continue;

        if (Slot.Count >= MaxStack) continue;

        // 해당 슬롯에 남은 공간 
        const int32 Space = MaxStack - Slot.Count;
        // 남은 공간과 남은 개수 중 더 작은 것을 선택
        const int32 AddCount = FMath::Min(Space, Remaining);

        Slot.Count += AddCount;
        Remaining -= AddCount;
        AddedCount += AddCount;
    }

    // 2. 남은 수량을 빈 슬롯에 가능한 만큼 추가
    for (FInventorySlot& Slot : TargetSlots)
    {
        if (Remaining <= 0) break;    

        if (!Slot.IsEmpty()) continue;

        const int32 AddCount = FMath::Min(MaxStack, Remaining);

        Slot.ItemTag = ItemTag;
        Slot.Count = AddCount;

        Remaining -= AddCount;
        AddedCount += AddCount;
    }

    if (AddedCount > 0)
    {
        if (ItemTab == ActiveTab)
        {
            InventorySlots = TargetSlots;
        }
        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
    }

    return AddedCount;
}

bool UInventoryComponent::RemoveMaterial(const FGameplayTag& ItemTag, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
    {
        return false;
    }

    if (GetMaterialCount(ItemTag) < Amount)
    {
        return false;
    }

    FInventoryTabPage* Page = FindMutablePage(GetInventoryTabForItem(ItemTag));
    if (!Page)
    {
        return false;
    }

    int32 Remaining = Amount;

    for (FInventorySlot& Slot : Page->Slots) 
    {
        if (ItemTag != Slot.ItemTag) continue;

        const int32 RemoveCount = FMath::Min(Remaining, Slot.Count);

        Slot.Count -= RemoveCount;
        Remaining -= RemoveCount;

        if (Slot.Count <= 0)
        {
            Slot.Clear();
        }

        if (Remaining <= 0)
        {
            if (Page->Tab == ActiveTab)
            {
                InventorySlots = Page->Slots;
            }
            OnInventoryChanged.Broadcast();
            PrintInventoryToScreen();
            return true;
        }
    }

    return false;
}

int32 UInventoryComponent::GetMaterialCount(const FGameplayTag& ItemTag) const
{
    int32 TotalCount = 0;

    for (const FInventoryTabPage& Page : InventoryPages)
    {
        for (const FInventorySlot& Slot : Page.Slots)
        {
            if (Slot.ItemTag == ItemTag)
            {
                TotalCount += Slot.Count;
            }
        }
    }

    return TotalCount;
}

void UInventoryComponent::BroadcastShipUpgradeInventoryChanged()
{
	ShipUpgradeInventoryChanged.Broadcast();
}

int32 UInventoryComponent::AddItem(const FGameplayTag& ItemTag, int32 Amount)
{
	return AddMaterial(ItemTag, Amount);
}

bool UInventoryComponent::RemoveItem(const FGameplayTag& ItemTag, int32 Amount)
{
	return RemoveMaterial(ItemTag, Amount);
}

int32 UInventoryComponent::GetItemCount(const FGameplayTag& ItemTag) const
{
	return GetMaterialCount(ItemTag);
}

bool UInventoryComponent::CanAddItem(const FGameplayTag& ItemTag, int32 Amount) const
{
	if (!ItemTag.IsValid() || Amount <= 0)
	{
		return false;
	}
	const EInventoryTab ItemTab = GetInventoryTabForItem(ItemTag);
	const FInventoryTabPage* Page = FindPage(ItemTab);
	if (!Page)
	{
		return false;
	}

	TArray<FInventorySlot> WorkingSlots = Page->Slots;
	return AddToSlots(WorkingSlots, ItemTag, Amount, GetMaxStack(ItemTag));
}

bool UInventoryComponent::TryApplyCraftingTransaction(const TArray<FCraftingItemStack>& Costs, const FCraftingItemStack& Result)
{
	TArray<FCraftingItemStack> Results;
	Results.Add(Result);
	return TryApplyItemTransaction(Costs, Results);
}

bool UInventoryComponent::RemoveItemsAtomically(const TArray<FCraftingItemStack>& Costs)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	InitializeInventoryPages();
	TArray<FInventoryTabPage> WorkingPages = InventoryPages;
	for (const FCraftingItemStack& Cost : Costs)
	{
		if (!Cost.ItemTag.IsValid() || Cost.Quantity <= 0)
		{
			return false;
		}

		FInventoryTabPage* CostPage = FindPageInArray(WorkingPages, GetInventoryTabForItem(Cost.ItemTag));
		if (!CostPage || !RemoveFromSlots(CostPage->Slots, Cost.ItemTag, Cost.Quantity))
		{
			return false;
		}
	}

	InventoryPages = MoveTemp(WorkingPages);
	if (const FInventoryTabPage* ActivePage = FindPage(ActiveTab))
	{
		InventorySlots = ActivePage->Slots;
	}
	OnInventoryChanged.Broadcast();
	PrintInventoryToScreen();
	return true;
}

bool UInventoryComponent::AddItemsAtomically(const TArray<FCraftingItemStack>& Items)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	InitializeInventoryPages();
	TArray<FInventoryTabPage> WorkingPages = InventoryPages;
	for (const FCraftingItemStack& Item : Items)
	{
		if (!Item.ItemTag.IsValid() || Item.Quantity <= 0)
		{
			return false;
		}

		FInventoryTabPage* ItemPage = FindPageInArray(WorkingPages, GetInventoryTabForItem(Item.ItemTag));
		if (!ItemPage || !AddToSlots(ItemPage->Slots, Item.ItemTag, Item.Quantity, GetMaxStack(Item.ItemTag)))
		{
			return false;
		}
	}

	InventoryPages = MoveTemp(WorkingPages);
	if (const FInventoryTabPage* ActivePage = FindPage(ActiveTab))
	{
		InventorySlots = ActivePage->Slots;
	}
	OnInventoryChanged.Broadcast();
	PrintInventoryToScreen();
	return true;
}

bool UInventoryComponent::CanApplyItemTransaction(
	const TArray<FCraftingItemStack>& RemovedItems,
	const TArray<FCraftingItemStack>& AddedItems) const
{
	TArray<FInventoryTabPage> WorkingPages = InventoryPages;
	if (WorkingPages.IsEmpty())
	{
		return RemovedItems.IsEmpty() && AddedItems.IsEmpty();
	}

	for (const FCraftingItemStack& Item : RemovedItems)
	{
		if (!Item.ItemTag.IsValid() || Item.Quantity <= 0)
		{
			return false;
		}
		FInventoryTabPage* Page = FindPageInArray(WorkingPages, GetInventoryTabForItem(Item.ItemTag));
		if (!Page || !RemoveFromSlots(Page->Slots, Item.ItemTag, Item.Quantity))
		{
			return false;
		}
	}
	for (const FCraftingItemStack& Item : AddedItems)
	{
		if (!Item.ItemTag.IsValid() || Item.Quantity <= 0)
		{
			return false;
		}
		FInventoryTabPage* Page = FindPageInArray(WorkingPages, GetInventoryTabForItem(Item.ItemTag));
		if (!Page || !AddToSlots(Page->Slots, Item.ItemTag, Item.Quantity, GetMaxStack(Item.ItemTag)))
		{
			return false;
		}
	}
	return true;
}

bool UInventoryComponent::TryApplyItemTransaction(
	const TArray<FCraftingItemStack>& RemovedItems,
	const TArray<FCraftingItemStack>& AddedItems)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	InitializeInventoryPages();
	TArray<FInventoryTabPage> WorkingPages = InventoryPages;
	for (const FCraftingItemStack& Item : RemovedItems)
	{
		if (!Item.ItemTag.IsValid() || Item.Quantity <= 0)
		{
			return false;
		}
		FInventoryTabPage* Page = FindPageInArray(WorkingPages, GetInventoryTabForItem(Item.ItemTag));
		if (!Page || !RemoveFromSlots(Page->Slots, Item.ItemTag, Item.Quantity))
		{
			return false;
		}
	}
	for (const FCraftingItemStack& Item : AddedItems)
	{
		if (!Item.ItemTag.IsValid() || Item.Quantity <= 0)
		{
			return false;
		}
		FInventoryTabPage* Page = FindPageInArray(WorkingPages, GetInventoryTabForItem(Item.ItemTag));
		if (!Page || !AddToSlots(Page->Slots, Item.ItemTag, Item.Quantity, GetMaxStack(Item.ItemTag)))
		{
			return false;
		}
	}

	InventoryPages = MoveTemp(WorkingPages);
	if (const FInventoryTabPage* ActivePage = FindPage(ActiveTab))
	{
		InventorySlots = ActivePage->Slots;
	}
	OnInventoryChanged.Broadcast();
	PrintInventoryToScreen();
	return true;
}

int32 UInventoryComponent::GetMaxStack(const FGameplayTag& ItemTag) const
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

UTexture2D* UInventoryComponent::GetMaterialIcon(const FGameplayTag& ItemTag) const
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

FText UInventoryComponent::GetMaterialName(const FGameplayTag& ItemTag) const
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

FText UInventoryComponent::GetItemDescription(const FGameplayTag& ItemTag) const
{
    if (UWorld* World = GetWorld())
    {
        if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
        {
            return Subsystem->GetHowToInteractText(ItemTag);
        }
    }
    return FText::GetEmpty();
}

FGameplayTag UInventoryComponent::GetItemRarityTag(const FGameplayTag& ItemTag) const
{
    if (UWorld* World = GetWorld())
    {
        if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
        {
            return Subsystem->GetRarityTag(ItemTag);
        }
    }
    return FGameplayTag();
}

FText UInventoryComponent::GetItemRarityName(const FGameplayTag& ItemTag) const
{
    const FGameplayTag RarityTag = GetItemRarityTag(ItemTag);
    if (!RarityTag.IsValid())
    {
        return FText::GetEmpty();
    }

    if (RarityTag.MatchesTagExact(Item_Rarity_Common)) return FText::FromString(TEXT("Common"));
    if (RarityTag.MatchesTagExact(Item_Rarity_Rare)) return FText::FromString(TEXT("Rare"));
    if (RarityTag.MatchesTagExact(Item_Rarity_Epic)) return FText::FromString(TEXT("Epic"));
    if (RarityTag.MatchesTagExact(Item_Rarity_Legendary)) return FText::FromString(TEXT("Legendary"));
    if (RarityTag.MatchesTagExact(Item_Rarity_Relic)) return FText::FromString(TEXT("Relic"));

    return FText::FromString(RarityTag.GetTagName().ToString());
}

EInventoryTab UInventoryComponent::GetInventoryTabForItem(const FGameplayTag& ItemTag) const
{
    if (UWorld* World = GetWorld())
    {
        if (UItemSubsystem* Subsystem = World->GetSubsystem<UItemSubsystem>())
        {
            const FGameplayTag CategoryTag = Subsystem->GetCategoryTag(ItemTag);
            if (CategoryTag.MatchesTag(Item_Category_Clue)) return EInventoryTab::Clue;
            if (CategoryTag.MatchesTag(Item_Category_Consumable)) return EInventoryTab::Consumable;
            if (CategoryTag.MatchesTag(Item_Category_Material)) return EInventoryTab::Material;
            if (CategoryTag.MatchesTag(Item_Category_Weapon)) return EInventoryTab::Weapon;
        }
    }

    if (ItemTag.MatchesTag(Item_Id_Clue)) return EInventoryTab::Clue;
    if (ItemTag.MatchesTag(Item_Id_Consumables)) return EInventoryTab::Consumable;
    if (ItemTag.MatchesTag(Item_Id_Weapon)) return EInventoryTab::Weapon;

    return EInventoryTab::Material;
}

void UInventoryComponent::HandleLeftClickSlot(int32 SlotIndex)
{
    HandleLeftClickSlotInTab(ActiveTab, SlotIndex);
}

void UInventoryComponent::HandleLeftClickSlotInTab(EInventoryTab Tab, int32 SlotIndex)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    InitializeInventoryPages();

    FInventoryTabPage* Page = FindMutablePage(Tab);
    if (!Page || !Page->Slots.IsValidIndex(SlotIndex))
    {
        return;
    }

    TArray<FInventorySlot>& Slots = Page->Slots;
    FInventorySlot& TargetSlot = Slots[SlotIndex];

    // 1. 커서가 비어 있고, 아이템 슬롯을 클릭한 경우
    if (!CursorItem.IsValid())
    {
        if (TargetSlot.IsEmpty())
        {
            return;
        }

        CursorItem.ItemTag = TargetSlot.ItemTag;
        CursorItem.Count = TargetSlot.Count;
        CursorItem.OriginalSlotIndex = SlotIndex;
        CursorItem.OriginalTab = Tab;

        TargetSlot.Clear();

        if (Tab == ActiveTab)
        {
            InventorySlots = Slots;
        }
        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 2. 커서가 아이템을 들고 있고, 빈 슬롯을 클릭한 경우
    if (TargetSlot.IsEmpty())
    {
        if (!CanSlotAcceptItem(Tab, CursorItem.ItemTag))
        {
            return;
        }

        TargetSlot.ItemTag = CursorItem.ItemTag;
        TargetSlot.Count = CursorItem.Count;

        CursorItem.Clear();

        if (Tab == ActiveTab)
        {
            InventorySlots = Slots;
        }
        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 3. 커서 아이템과 대상 슬롯 아이템이 같은 경우
    // 가능한 만큼만 합치기
    if (TargetSlot.ItemTag == CursorItem.ItemTag)
    {
        if (!CanSlotAcceptItem(Tab, CursorItem.ItemTag))
        {
            return;
        }

        const int32 MaxStack = GetMaxStack(CursorItem.ItemTag);
        const int32 Space = MaxStack - TargetSlot.Count;

        if (Space <= 0)
        {
            return;
        }

        const int32 MoveCount = FMath::Min(Space, CursorItem.Count);

        TargetSlot.Count += MoveCount;
        CursorItem.Count -= MoveCount;

        if (CursorItem.Count <= 0)
        {
            CursorItem.Clear();
        }

        if (Tab == ActiveTab)
        {
            InventorySlots = Slots;
        }
        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 4. 다른 아이템이면 서로 교환
    if (!CanSlotAcceptItem(Tab, CursorItem.ItemTag))
    {
        return;
    }

    FInventorySlot TempSlot = TargetSlot;

    TargetSlot.ItemTag = CursorItem.ItemTag;
    TargetSlot.Count = CursorItem.Count;

    CursorItem.ItemTag = TempSlot.ItemTag;
    CursorItem.Count = TempSlot.Count;
    CursorItem.OriginalSlotIndex = SlotIndex;
    CursorItem.OriginalTab = Tab;

    if (Tab == ActiveTab)
    {
        InventorySlots = Slots;
    }
    OnInventoryChanged.Broadcast();
    PrintInventoryToScreen();
}

void UInventoryComponent::HandleRightClickInventory()
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    ReturnCursorToOriginalSlot();
}
void UInventoryComponent::ReturnCursorToOriginalSlot()
{
    if (!CursorItem.IsValid())
    {
        return;
    }

    FInventoryTabPage* Page = FindMutablePage(CursorItem.OriginalTab);
    if (!Page || !Page->Slots.IsValidIndex(CursorItem.OriginalSlotIndex))
    {
        CursorItem.Clear();

        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    TArray<FInventorySlot>& Slots = Page->Slots;
    FInventorySlot& OriginalSlot = Slots[CursorItem.OriginalSlotIndex];

    // 원래 칸이 비어 있으면 그대로 복귀
    if (OriginalSlot.IsEmpty())
    {
        OriginalSlot.ItemTag = CursorItem.ItemTag;
        OriginalSlot.Count = CursorItem.Count;

        CursorItem.Clear();

        if (Page->Tab == ActiveTab)
        {
            InventorySlots = Slots;
        }
        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 원래 칸에 같은 아이템이 있으면 가능한 만큼 합치기
    if (OriginalSlot.ItemTag == CursorItem.ItemTag)
    {
        const int32 MaxStack = GetMaxStack(CursorItem.ItemTag);
        const int32 Space = MaxStack - OriginalSlot.Count;
        const int32 MoveCount = FMath::Min(Space, CursorItem.Count);

        OriginalSlot.Count += MoveCount;
        CursorItem.Count -= MoveCount;

        if (CursorItem.Count <= 0)
        {
            CursorItem.Clear();
        }

        if (Page->Tab == ActiveTab)
        {
            InventorySlots = Slots;
        }
        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 예외 상황: 원래 칸에 다른 아이템이 있으면 교환
    FInventorySlot TempSlot = OriginalSlot;

    OriginalSlot.ItemTag = CursorItem.ItemTag;
    OriginalSlot.Count = CursorItem.Count;

    CursorItem.ItemTag = TempSlot.ItemTag;
    CursorItem.Count = TempSlot.Count;
    CursorItem.OriginalSlotIndex = TempSlot.IsEmpty() ? INDEX_NONE : CursorItem.OriginalSlotIndex;
    CursorItem.OriginalTab = Page->Tab;

    if (Page->Tab == ActiveTab)
    {
        InventorySlots = Slots;
    }
    OnInventoryChanged.Broadcast();
    PrintInventoryToScreen();
}

int32 UInventoryComponent::TransferSlotToStorage(int32 SlotIndex, UStorageComponent* TargetStorage)
{
    // 인벤토리의 슬롯을, storage로 이동
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetStorage)
    {
        return 0;
    }

    if (CursorItem.IsValid())
    {
        ReturnCursorToOriginalSlot();
    }

    FInventoryTabPage* Page = FindMutablePage(ActiveTab);
    if (!Page || !Page->Slots.IsValidIndex(SlotIndex) || Page->Slots[SlotIndex].IsEmpty())
    {
        return 0;
    }

    FInventorySlot& SourceSlot = Page->Slots[SlotIndex];
    const int32 AddedCount = TargetStorage->AddItem(SourceSlot.ItemTag, SourceSlot.Count);

    if (AddedCount <= 0)
    {
        return 0;
    }

    SourceSlot.Count -= AddedCount;
    if (SourceSlot.Count <= 0)
    {
        SourceSlot.Clear();
    }

    InventorySlots = Page->Slots;
    OnInventoryChanged.Broadcast();
    PrintInventoryToScreen();

    return AddedCount;
}

int32 UInventoryComponent::TransferCursorToStorageSlot(UStorageComponent* TargetStorage, int32 StorageSlotIndex)
{
    // 커서에 붙은 아이템을 storage로 이동
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetStorage || !CursorItem.IsValid())
    {
        return 0;
    }

    const int32 AddedCount = TargetStorage->AddItemToSlot(StorageSlotIndex, CursorItem.ItemTag, CursorItem.Count);

    if (AddedCount <= 0)
    {
        return 0;
    }

    CursorItem.Count -= AddedCount;
    if (CursorItem.Count <= 0)
    {
        CursorItem.Clear();
    }

    OnInventoryChanged.Broadcast();
    PrintInventoryToScreen();

    return AddedCount;
}

void UInventoryComponent::ServerHandleRightClickInventory_Implementation()
{
    HandleRightClickInventory();
}

void UInventoryComponent::ServerHandleLeftClickSlot_Implementation(int32 SlotIndex)
{
    HandleLeftClickSlot(SlotIndex);
}

void UInventoryComponent::ServerHandleLeftClickSlotInTab_Implementation(EInventoryTab Tab, int32 SlotIndex)
{
    HandleLeftClickSlotInTab(Tab, SlotIndex);
}

void UInventoryComponent::PrintInventoryToScreen() const
{
    if (!GEngine)
    {
        return;
    }

    FString DebugText = TEXT("[Inventory]\n");

    bool bHasAnyItem = false;

    for (int32 Index = 0; Index < InventorySlots.Num(); ++Index)
    {
        const FInventorySlot& Slot = InventorySlots[Index];

        if (Slot.IsEmpty())
        {
            continue;
        }

        bHasAnyItem = true;

        DebugText += FString::Printf(
            TEXT("[%d] %s : %d\n"),
            Index,
            *Slot.ItemTag.ToString(),
            Slot.Count
        );
    }

    if (!bHasAnyItem)
    {
        DebugText += TEXT("Empty\n");
    }

    if (CursorItem.IsValid())
    {
        DebugText += FString::Printf(
            TEXT("\n[Cursor] %s : %d / OriginalSlot: %d\n"),
            *CursorItem.ItemTag.ToString(),
            CursorItem.Count,
            CursorItem.OriginalSlotIndex
        );
    }

    GEngine->AddOnScreenDebugMessage(
        7777,
        3.0f,
        FColor::Green,
        DebugText
    );
}
