// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "InventoryComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnInventoryChanged);

class UItemData;
class UTexture2D;

/*
* 인벤토리 한 칸에 담는 정보를 담당하는 구조체 (아이템 개수, 태그)
*/
USTRUCT(BlueprintType)
struct FInventoryMaterialEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	FGameplayTag ItemTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;
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

	// 리플리케이션을 위함
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 인벤토리에 아이템 추가
	 * @param ItemTag 추가할 아이템의 태그 (ex. Item.Material.Ore)
	 * @param Amount 추가할 아이템의 양 (ex. 1, 2, 3, ...)
	 * @return 추가 성공시 true
	 */
	bool AddMaterial(const FGameplayTag& ItemTag, int32 Amount = 1);

	/**
	 * 인벤토리에 아이템 제거
	 * @param ItemTag 제거할 아이템의 태그 (ex. Item.Material.Ore)
	 * @param Amount 제거할 아이템의 양 (ex. 1, 2, 3, ...)
	 * @return 제거 성공시 true
	 */
	bool RemoveMaterial(const FGameplayTag& ItemTag, int32 Amount = 1);

	/**
	 * 인벤토리 배열 Getter
	 * @return Materials 인벤토리 배열
	 */
	const TArray<FInventoryMaterialEntry>& GetMaterials() const { return InventoryContents; }

	/**
	 * 해당 태그의 아이템 개수 Getter
	 * @param ItemTag 개수 확인이 필요한 아이템 태그  (ex. Item.Material.Ore)
	 * @return 인벤토리 배열 내 동일한 태그 아이템 개수 반환
	 */
	int32 GetMaterialCount(const FGameplayTag& ItemTag) const;

	// 인벤토리에 변경 사항이 있을 때 브로드캐스트 되는 델리게이트 ex. UI 업데이트 
	FOnInventoryChanged OnInventoryChanged;

	// 인벤토리 내 아이템 아이콘 Getter
	UTexture2D* GetMaterialIcon(const FGameplayTag& ItemTag) const;

	// 인벤토리 내 아이템 이름 Getter
	FText GetMaterialName(const FGameplayTag& ItemTag) const;

protected:
	// 인벤토리 내 아이템을 보관하는 배열 <FInventoryMaterialEntry>
	// 서버로부터 새 Materials 배열을 받았을 때, OnRep_InventoryContents를 자동으로 호출하여 브로드캐스트 하게 함
	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_InventoryContents, Category = "Inventory")
	TArray<FInventoryMaterialEntry> InventoryContents;

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
