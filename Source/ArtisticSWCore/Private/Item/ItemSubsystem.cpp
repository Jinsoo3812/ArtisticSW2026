// Fill out your copyright notice in the Description page of Project Settings.


#include "ItemSubsystem.h"
#include "ItemData.h"
#include "Settings_Item.h"
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
	const USettings_Item* Settings = GetDefault<USettings_Item>();
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

	// FItemRecipeData 데이터 테이블 캐싱
	if (UDataTable* RecipeDT = Settings->ItemRecipeDataTable.LoadSynchronous())
	{
		static const FString ContextString(TEXT("ItemRecipeData Initialization"));
		TArray<FItemRecipeData*> AllRecipes;
		RecipeDT->GetAllRows<FItemRecipeData>(ContextString, AllRecipes);

		for (FItemRecipeData* Recipe : AllRecipes)
		{
			if (Recipe && Recipe->RequiredIngredients.Num() > 0)
			{
				// 재료 목록을 해싱하여 키 값으로 사용
				uint32 Hash = GenerateRecipeHash(Recipe->RequiredIngredients);
				CachedRecipeData.Add(Hash, *Recipe);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[ItemSubsystem] Successfully cached %d Item Recipes."), CachedRecipeData.Num());
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
	}

	// [지연 스폰 시작] - BeginPlay가 호출되기 전에 액터를 메모리에만 올림
	ABaseItem* SpawnedItem = World->SpawnActorDeferred<ABaseItem>(
		SpawnClass,
		SpawnTransform,
		Instigator,
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
	// SpawnTrasnform의 scale만 로그로 출력
	UE_LOG(LogTemp, Log, TEXT("Spawned Item with Tag: Scale: %s"),
		*SpawnTransform.GetScale3D().ToString()
	);
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

TSoftObjectPtr<UTexture2D> UItemSubsystem::GetIcon2D(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->Icon2D;
	return nullptr;
}

TSoftObjectPtr<UStaticMesh> UItemSubsystem::GetItemMesh(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->ItemMesh;
	return nullptr;
}

TSoftClassPtr<UGameplayAbility> UItemSubsystem::GetGrantedAbilityClass(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->GrantedAbilityClass;
	return nullptr;
}

TSoftClassPtr<ABaseItem> UItemSubsystem::GetSpawnClassByCrafting(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->SpawnClassByCrafting;
	return nullptr;
}

TSoftClassPtr<AActor> UItemSubsystem::GetSpawnClass(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->SpawnClass;
	return nullptr;
}

TArray<FGameplayTag> UItemSubsystem::GetCanUseClassList(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->CanUseClassList;
	return TArray<FGameplayTag>();
}

FGameplayTag UItemSubsystem::GetUseKeyTag(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->UseKeyTag;
	return FGameplayTag::EmptyTag;
}

FGameplayTag UItemSubsystem::GetRarityTag(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->RarityTag;
	return FGameplayTag::EmptyTag;
}

FGameplayTag UItemSubsystem::GetCategoryTag(const FGameplayTag& ItemTag) const
{
	if (const FItemDefinition* Def = GetItemDefinition(ItemTag)) return Def->CategoryTag;
	return FGameplayTag::EmptyTag;
}

FText UItemSubsystem::GetItemName(const FGameplayTag& ItemTag) const
{
	if (const FItemFeatureData* Feature = GetItemFeature(ItemTag)) return Feature->ItemName;
	return FText::FromString(ItemTag.ToString());
}

FText UItemSubsystem::GetHowToInteractText(const FGameplayTag& ItemTag) const
{
	if (const FItemFeatureData* Feature = GetItemFeature(ItemTag)) return Feature->HowToInteractText;
	return FText::GetEmpty();
}

FName UItemSubsystem::GetAttachmentSocketName(const FGameplayTag& ItemTag) const
{
	if (const FItemFeatureData* Feature = GetItemFeature(ItemTag)) return Feature->AttachmentSocketName;
	return FName("GripPoint");
}

int32 UItemSubsystem::GetMaxStack(const FGameplayTag& ItemTag) const
{
	if (const FItemFeatureData* Feature = GetItemFeature(ItemTag)) return Feature->MaxStack;
	return 99;
}

uint32 UItemSubsystem::GenerateRecipeHash(const TMap<FGameplayTag, int32>& Ingredients) const
{
	// 재료의 순서가 뒤죽박죽이어도 똑같은 해시가 나오도록 태그 문자열 기준으로 정렬.
	TArray<FString> SortedKeys;
	for (const auto& Pair : Ingredients)
	{
		// 예: "Item.Material.Apple:2" 형태의 문자열 생성
		FString KeyStr = FString::Printf(TEXT("%s:%d"), *Pair.Key.ToString(), Pair.Value);
		SortedKeys.Add(KeyStr);
	}

	// 알파벳 순 정렬
	SortedKeys.Sort();

	// 정렬된 문자열들을 하나로 쫙 이어 붙임
	FString CombinedString = TEXT("");
	for (const FString& Str : SortedKeys)
	{
		CombinedString += Str;
	}

	// 이어 붙인 문자열의 고유 Hash값 리턴
	return GetTypeHash(CombinedString);
}

const FItemRecipeData* UItemSubsystem::FindRecipe(const TMap<FGameplayTag, int32>& InputIngredients) const
{
	// 인풋으로 들어온 재료들의 해시를 생성
	uint32 Hash = GenerateRecipeHash(InputIngredients);

	// 해시맵에서 O(1)로 찾기
	if (const FItemRecipeData* FoundRecipe = CachedRecipeData.Find(Hash))
	{
		return FoundRecipe;
	}
	return nullptr;
}

