// Fill out your copyright notice in the Description page of Project Settings.


#include "ItemSubsystem.h"
#include "ItemData.h"
#include "Engine/World.h"

void UItemSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadItemData();
}

void UItemSubsystem::Deinitialize()
{
	CachedItemData = nullptr;
	Super::Deinitialize();
}

void UItemSubsystem::LoadItemData()
{
	// 실제 프로젝트 환경에 맞게 DA 경로 지정
	FSoftObjectPath ItemDataPath(TEXT("/Game/Blueprints/Item/DA_ItemData.DA_ItemData"));

	// 동기 로드
	CachedItemData = Cast<UItemData>(ItemDataPath.TryLoad());

	if (!CachedItemData)
	{
		UE_LOG(LogTemp, Error, TEXT("ItemSubsystem: Failed to load UItemData!"));
	}
}

ABaseItem* UItemSubsystem::SpawnItem(const FGameplayTag& ItemTag, const FTransform& SpawnTransform, EItemState InitialState, AActor* Instigator)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !ItemTag.IsValid()) return nullptr;

	const FItemDefinition* Def = GetItemDefinition(ItemTag);
	if (!Def)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemSubsystem: Invalid ItemTag %s"), *ItemTag.ToString());
		return nullptr;
	}

	UClass* SpawnClass = Def->SpawnClassByCrafting.LoadSynchronous();
	if (!SpawnClass)
	{
		SpawnClass = ABaseItem::StaticClass(); // Fallback
		UE_LOG(LogTemp, Warning, TEXT("ItemSubsystem: SpawnClass not found for ItemTag %s, using ABaseItem as fallback"), *ItemTag.ToString());
	}

	// [지연 스폰 시작] - BeginPlay가 호출되기 전에 액터를 메모리에만 올림
	ABaseItem* SpawnedItem = World->SpawnActorDeferred<ABaseItem>(
		SpawnClass,
		SpawnTransform,
		nullptr,
		Instigator ? Cast<APawn>(Instigator) : nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	);

	// 스폰에 성공한 후 BaseItem 초기화
	if (SpawnedItem)
	{
		SpawnedItem->ItemTag = ItemTag;

		// BaseItem의 BeginPlay 호출
		SpawnedItem->FinishSpawning(SpawnTransform);

		// 상태 변경 (초기화)
		SpawnedItem->SetItemState(InitialState);
	}

	return SpawnedItem;
}

const FItemDefinition* UItemSubsystem::GetItemDefinition(const FGameplayTag& ItemTag) const
{
	if (CachedItemData)
	{
		return CachedItemData->FindItemDefinition(ItemTag);
	}
	return nullptr;
}

