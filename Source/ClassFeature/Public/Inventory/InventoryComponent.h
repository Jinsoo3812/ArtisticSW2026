// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "InventoryComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnInventoryChanged);

class UItemData;
class UStorageComponent;
class UTexture2D;

USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	FGameplayTag ItemTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;

	bool IsEmpty() const
	{
		return !ItemTag.IsValid() || Count <= 0;
	}

	void Clear()
	{
		ItemTag = FGameplayTag();
		Count = 0;
	}
};

USTRUCT(BlueprintType)
struct FInventoryCursorItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	FGameplayTag ItemTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 OriginalSlotIndex = INDEX_NONE;

	bool IsValid() const
	{
		return ItemTag.IsValid() && Count > 0 && OriginalSlotIndex != INDEX_NONE;
	}

	void Clear()
	{
		ItemTag = FGameplayTag();
		Count = 0;
		OriginalSlotIndex = INDEX_NONE;
	}
};

/*
* 인벤토리 컴포넌트 (Material.~ 아이템을 주울 때, 인벤토리로 들어옴)
*/
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CLASSFEATURE_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInventoryComponent();

	virtual void BeginPlay() override;

	// 리플리케이션을 위함
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//인벤토리에 아이템 추가
	int32 AddMaterial(const FGameplayTag& ItemTag, int32 Amount = 1);
	//인벤토리에 아이템 제거
	bool RemoveMaterial(const FGameplayTag& ItemTag, int32 Amount = 1);
	// Tag 아이템 총 개수 반환
	int32 GetMaterialCount(const FGameplayTag& ItemTag) const;

	// New generic item wrappers. Legacy Material functions remain unchanged.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Item")
	int32 AddItem(const FGameplayTag& ItemTag, int32 Amount = 1);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Item")
	bool RemoveItem(const FGameplayTag& ItemTag, int32 Amount = 1);

	UFUNCTION(BlueprintPure, Category = "Inventory|Item")
	int32 GetItemCount(const FGameplayTag& ItemTag) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Item")
	bool CanAddItem(const FGameplayTag& ItemTag, int32 Amount = 1) const;

	/** Atomic inventory destination used by crafting: costs and result commit together. */
	bool TryApplyCraftingTransaction(const TArray<FCraftingItemStack>& Costs, const FCraftingItemStack& Result);

	/** Atomic cost removal used before delivering to an external receiver. */
	bool RemoveItemsAtomically(const TArray<FCraftingItemStack>& Costs);

	/** Best-effort rollback path for a receiver that rejected after preflight. */
	bool AddItemsAtomically(const TArray<FCraftingItemStack>& Items);

	// 하나의 특정 슬롯 Getter
	const TArray<FInventorySlot>& GetSlots() const { return InventorySlots; }
	// 커서에 달린 아이템 Getter
	const FInventoryCursorItem& GetCursorItem() const { return CursorItem; }

	//인벤토리 Row Getter
	int32 GetInventoryRows() const { return InventoryRows; }
	//인벤토리 Col Getter
	int32 GetInventoryColumns() const { return InventoryColumns; }
	//인벤토리 총 슬롯 개수 Getter
	int32 GetSlotCount() const { return InventoryRows * InventoryColumns; }

	// 인벤토리 내 아이템 MaxStack Getter
	int32 GetMaxStack(const FGameplayTag& ItemTag) const;
	// 인벤토리 내 아이템 아이콘 Getter
	UTexture2D* GetMaterialIcon(const FGameplayTag& ItemTag) const;
	// 인벤토리 내 아이템 이름 Getter
	FText GetMaterialName(const FGameplayTag& ItemTag) const;

	// 인벤토리 좌클릭 
	void HandleLeftClickSlot(int32 SlotIndex);
	// 인벤토리 우클릭 
	void HandleRightClickInventory();
	// 커서 아이템 원래 위치로 복귀
	void ReturnCursorToOriginalSlot();
	int32 TransferSlotToStorage(int32 SlotIndex, UStorageComponent* TargetStorage);
	int32 TransferCursorToStorageSlot(UStorageComponent* TargetStorage, int32 StorageSlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerHandleLeftClickSlot(int32 SlotIndex);
	UFUNCTION(Server, Reliable)
	void ServerHandleRightClickInventory();

	// 인벤토리에 변경 사항이 있을 때 브로드캐스트 되는 델리게이트 ex. UI 업데이트 
	FOnInventoryChanged OnInventoryChanged;

protected:
	// 인벤토리 세로 칸 수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	int32 InventoryRows = 5;
	// 인벤토리 가로 칸 수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	int32 InventoryColumns = 6;
	// 인벤토리 슬롯 배열
	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_InventoryContents, Category = "Inventory")
	TArray<FInventorySlot> InventorySlots;
	// 커서에 붙어 있는 아이템
	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_InventoryContents, Category = "Inventory")
	FInventoryCursorItem CursorItem;

	// 인벤토리에서 아이템 조회를 위한 ItemData
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UItemData> ItemData;


	/**
	 * 브로드캐스트를 위한 함수
	 */
	UFUNCTION()
	void OnRep_InventoryContents();

	/*----디버깅용---*/
	void PrintInventoryToScreen() const;
};
