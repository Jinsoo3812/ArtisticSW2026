// Fill out your copyright notice in the Description page of Project Settings.


#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "ItemData.h"
#include "ItemSubsystem.h"
#include "Engine/Engine.h"
#include "Storage/StorageComponent.h"

// Sets default values for this component's properties
UInventoryComponent::UInventoryComponent()
{
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::BeginPlay()
{
    Super::BeginPlay();
    //서버에서만 인벤토리 array 크기 초기화
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        InventorySlots.SetNum(GetSlotCount());
    }
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UInventoryComponent, InventorySlots);
    DOREPLIFETIME(UInventoryComponent, CursorItem);
}

void UInventoryComponent::OnRep_InventoryContents()
{
    OnInventoryChanged.Broadcast();
}

int32 UInventoryComponent::AddMaterial(const FGameplayTag& ItemTag, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
    {
        return 0;
    }

    if (InventorySlots.Num() != GetSlotCount())
    {
        InventorySlots.SetNum(GetSlotCount());
    }

    const int32 MaxStack = GetMaxStack(ItemTag);

    int32 Remaining = Amount;
    int32 AddedCount = 0;
    
    // 1. 같은 아이템 슬롯에 먼저 가능한 만큼 누적
    for (FInventorySlot& Slot : InventorySlots)
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
    for (FInventorySlot& Slot : InventorySlots)
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

    int32 Remaining = Amount;

    for (FInventorySlot& Slot : InventorySlots) 
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

    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (Slot.ItemTag == ItemTag)
        {
            TotalCount += Slot.Count;
        }
    }

    return TotalCount;
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

void UInventoryComponent::HandleLeftClickSlot(int32 SlotIndex)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    if (!InventorySlots.IsValidIndex(SlotIndex))
    {
        return;
    }

    FInventorySlot& TargetSlot = InventorySlots[SlotIndex];

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

        TargetSlot.Clear();

        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 2. 커서가 아이템을 들고 있고, 빈 슬롯을 클릭한 경우
    if (TargetSlot.IsEmpty())
    {
        TargetSlot.ItemTag = CursorItem.ItemTag;
        TargetSlot.Count = CursorItem.Count;

        CursorItem.Clear();

        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 3. 커서 아이템과 대상 슬롯 아이템이 같은 경우
    // 가능한 만큼만 합치기
    if (TargetSlot.ItemTag == CursorItem.ItemTag)
    {
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

        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    // 4. 다른 아이템이면 서로 교환
    FInventorySlot TempSlot = TargetSlot;

    TargetSlot.ItemTag = CursorItem.ItemTag;
    TargetSlot.Count = CursorItem.Count;

    CursorItem.ItemTag = TempSlot.ItemTag;
    CursorItem.Count = TempSlot.Count;
    CursorItem.OriginalSlotIndex = SlotIndex;

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

    if (!InventorySlots.IsValidIndex(CursorItem.OriginalSlotIndex))
    {
        CursorItem.Clear();

        OnInventoryChanged.Broadcast();
        PrintInventoryToScreen();
        return;
    }

    FInventorySlot& OriginalSlot = InventorySlots[CursorItem.OriginalSlotIndex];

    // 원래 칸이 비어 있으면 그대로 복귀
    if (OriginalSlot.IsEmpty())
    {
        OriginalSlot.ItemTag = CursorItem.ItemTag;
        OriginalSlot.Count = CursorItem.Count;

        CursorItem.Clear();

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
    CursorItem.OriginalSlotIndex = INDEX_NONE;

    OnInventoryChanged.Broadcast();
    PrintInventoryToScreen();
}

int32 UInventoryComponent::TransferSlotToStorage(int32 SlotIndex, UStorageComponent* TargetStorage)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetStorage)
    {
        return 0;
    }

    if (CursorItem.IsValid())
    {
        ReturnCursorToOriginalSlot();
    }

    if (!InventorySlots.IsValidIndex(SlotIndex) || InventorySlots[SlotIndex].IsEmpty())
    {
        return 0;
    }

    FInventorySlot& SourceSlot = InventorySlots[SlotIndex];
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

    OnInventoryChanged.Broadcast();
    PrintInventoryToScreen();

    return AddedCount;
}

int32 UInventoryComponent::TransferCursorToStorageSlot(UStorageComponent* TargetStorage, int32 StorageSlotIndex)
{
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
