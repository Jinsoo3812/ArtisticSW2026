// Fill out your copyright notice in the Description page of Project Settings.


#include "ItemSubsystem.h"
#include "ItemData.h"
#include "ItemSettings.h"
#include "Engine/World.h"

void UItemSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	// 실제 게임 월드가 아닌 경우(에디터 프리뷰 등) 초기화(에셋 로드)를 스킵
	if (World && !World->IsGameWorld())
	{
		return;
	}

	// 프로젝트 세팅에 등록해둔 경로 가져오기
	const UItemSettings* Settings = GetDefault<UItemSettings>();
	if (!Settings) return;

	// FItemFeatureData 데이터 테이블 캐싱
	if (UDataTable* DT = Settings->ItemFeatureDataTable.LoadSynchronous())
	{
		static const FString ContextString(TEXT("ItemFeatureData Initialization"));
		TArray<FItemFeatureData*> AllRows;
		DT->GetAllRows<FItemFeatureData>(ContextString, AllRows);

		TArray<FName> RowNames = DT->GetRowNames();

		// DT의 데이터를 순회하면서 TMap에 복사
		for (int32 i = 0; i < RowNames.Num(); ++i)
		{
			FGameplayTag Tag = FGameplayTag::RequestGameplayTag(RowNames[i]);

			// 유효한 태그이고 데이터가 존재하면 Map에 적재
			if (Tag.IsValid() && AllRows[i] != nullptr)
			{
				CachedFeatureData.Add(Tag, *AllRows[i]);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[ItemSubsystem] Successfully cached %d Item Features from DataTable."), CachedFeatureData.Num());
	}

	// FItemDefinition 데이터 에셋(DA) 로드 및 참조 유지
	// DA 내부에는 이미 TMap이 구현되어 있으므로, 통째로 메모리에 띄워두고 포인터만 들고 있는다.
	if (UItemData* DA = Settings->ItemAssetRegistry.LoadSynchronous())
	{
		CachedItemData = DA;
		UE_LOG(LogTemp, Log, TEXT("[ItemSubsystem] Successfully loaded Item Asset Registry."));
	}
}

void UItemSubsystem::Deinitialize()
{
	CachedItemData = nullptr;
	Super::Deinitialize();
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

const FItemFeatureData* UItemSubsystem::GetItemFeature(const FGameplayTag& Tag) const
{
	// O(1) 해시 맵 탐색
	if (const FItemFeatureData* FoundData = CachedFeatureData.Find(Tag))
	{
		return FoundData;
	}
	return nullptr;
}

