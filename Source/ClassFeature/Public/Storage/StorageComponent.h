// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Inventory/InventoryComponent.h"
#include "StorageComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnStorageChanged);

class UTexture2D;

USTRUCT(BlueprintType)
struct FStorageItemEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 Count = 1;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API UStorageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStorageComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Storage")
	void ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems);

	UFUNCTION(BlueprintCallable, Category = "Storage")
	int32 AddItem(const FGameplayTag& ItemTag, int32 Amount = 1);

	UFUNCTION(BlueprintCallable, Category = "Storage")
	bool RemoveItem(const FGameplayTag& ItemTag, int32 Amount = 1);

	int32 AddItemToSlot(int32 SlotIndex, const FGameplayTag& ItemTag, int32 Amount = 1);
	int32 TransferSlotToInventory(int32 SlotIndex, UInventoryComponent* TargetInventory);

	const TArray<FInventorySlot>& GetSlots() const { return StorageSlots; }
	int32 GetStorageRows() const;
	int32 GetStorageColumns() const { return FMath::Max(1, ColumnCount); }
	int32 GetSlotCount() const { return FMath::Max(1, SlotCount); }

	int32 GetMaxStack(const FGameplayTag& ItemTag) const;
	UTexture2D* GetItemIcon(const FGameplayTag& ItemTag) const;
	FText GetItemName(const FGameplayTag& ItemTag) const;

	FOnStorageChanged OnStorageChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_StorageContents, Category = "Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 SlotCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_StorageContents, Category = "Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 ColumnCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage")
	TArray<FStorageItemEntry> InitialItems;

	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_StorageContents, Category = "Storage")
	TArray<FInventorySlot> StorageSlots;

	UFUNCTION()
	void OnRep_StorageContents();

	void InitializeFromInitialItems();
	void EnsureSlotArray();
	void CompactSlots();
	void BroadcastStorageChanged();
};
