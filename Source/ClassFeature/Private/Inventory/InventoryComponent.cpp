// Fill out your copyright notice in the Description page of Project Settings.


#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "ItemData.h"
#include "Engine/Engine.h"

// Sets default values for this component's properties
UInventoryComponent::UInventoryComponent()
{
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UInventoryComponent, InventoryContents);
}

void UInventoryComponent::OnRep_InventoryContents()
{
    OnInventoryChanged.Broadcast();
}

bool UInventoryComponent::AddMaterial(const FGameplayTag& ItemTag, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
    {
        return false;
    }

    // 기존에 동일 태그로 아이템이 있는 지 확인
    for (FInventoryMaterialEntry& Entry : InventoryContents)
    {
        if (Entry.ItemTag == ItemTag)
        {
            Entry.Count += Amount;
            OnInventoryChanged.Broadcast();
            // 디버깅용
            PrintInventoryToScreen();
            return true;
        }
    }

    // 동일 태그가 없다면, 새로운 구조체 생성 후 배열에 추가
    FInventoryMaterialEntry NewEntry;
    NewEntry.ItemTag = ItemTag;
    NewEntry.Count = Amount;
    InventoryContents.Add(NewEntry);

    OnInventoryChanged.Broadcast();
    //디버깅용
    PrintInventoryToScreen();
    return true;
}

bool UInventoryComponent::RemoveMaterial(const FGameplayTag& ItemTag, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemTag.IsValid() || Amount <= 0)
    {
        return false;
    }

    // Amount 만큼 인벤토리에서 아이템 Count 감소, 0 이하가 되면 배열에서 제거
    for (int32 i = 0; i < InventoryContents.Num(); ++i)
    {
        if (InventoryContents[i].ItemTag == ItemTag)
        {
            InventoryContents[i].Count -= Amount;

            if (InventoryContents[i].Count <= 0)
            {
                InventoryContents.RemoveAt(i);
            }

            OnInventoryChanged.Broadcast();
            return true;
        }
    }

    return false;
}

int32 UInventoryComponent::GetMaterialCount(const FGameplayTag& ItemTag) const
{
    for (const FInventoryMaterialEntry& Entry : InventoryContents)
    {
        // 인벤토리 배열 내 동일한 태그 아이템 Count 반환
        if (Entry.ItemTag == ItemTag)
        {
            return Entry.Count;
        }
    }

    return 0;
}


void UInventoryComponent::PrintInventoryToScreen() const
{
    if (!GEngine)
    {
        return;
    }

    FString DebugText = TEXT("[Inventory]\n");

    if (InventoryContents.Num() == 0)
    {
        DebugText += TEXT("Empty");
    }
    else
    {
        for (const FInventoryMaterialEntry& Entry : InventoryContents)
        {
            DebugText += FString::Printf(
                TEXT("- %s : %d\n"),
                *Entry.ItemTag.ToString(),
                Entry.Count
            );
        }
    }

    GEngine->AddOnScreenDebugMessage(
        7777,
        3.0f,
        FColor::Green,
        DebugText
    );
}

UTexture2D* UInventoryComponent::GetMaterialIcon(const FGameplayTag& ItemTag) const
{
    if (!ItemData)
    {
        return nullptr;
    }

    return ItemData->GetIconByTag(ItemTag);
}

FText UInventoryComponent::GetMaterialName(const FGameplayTag& ItemTag) const
{
    if (!ItemData)
    {
        return FText::FromString(ItemTag.ToString());
    }

    return ItemData->GetItemNameByTag(ItemTag);
}