#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "BaseItem.h"
#include "ItemData.h"
#include "ItemSubsystem.generated.h"

class UItemData;
struct FItemDefinition;

/*
* ItemData DA를 제공하고
* BaseItem을 소환 및 관리하는 Subsystem
*/
UCLASS()
class ARTISTICSWCORE_API UItemSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Subsystem 초기화 및 해제
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * [서버]아이템을 지연 스폰(Deferred Spawn) 방식으로 생성
	 * @param ItemTag - 생성할 아이템의 태그
	 * @param SpawnTransform - 스폰 위치 및 회전
	 * @param InitialState - 아이템의 초기 상태 (예: 땅에 떨어짐, 장착됨 등)
	 * @param Instigator - 생성자
	 */
	UFUNCTION()
	ABaseItem* SpawnItem(const FGameplayTag& ItemTag, const FTransform& SpawnTransform, EItemState InitialState, AActor* Instigator = nullptr);

	// GameplayTag로 에셋 데이터(오브젝트/클래스) 가져오기
	const FItemDefinition* GetItemDefinition(const FGameplayTag& ItemTag) const;

	// GameplayTag로 피처 데이터(수치) 가져오기
	const FItemFeatureData* GetItemFeature(const FGameplayTag& Tag) const;

	// FItemDefinition Property Getters
	TSoftObjectPtr<UTexture2D> GetIcon2D(const FGameplayTag& ItemTag) const;
	TSoftObjectPtr<UStaticMesh> GetItemMesh(const FGameplayTag& ItemTag) const;
	TSoftClassPtr<UGameplayAbility> GetGrantedAbilityClass(const FGameplayTag& ItemTag) const;
	TSoftClassPtr<ABaseItem> GetSpawnClassByCrafting(const FGameplayTag& ItemTag) const;
	TSoftClassPtr<AActor> GetSpawnClass(const FGameplayTag& ItemTag) const;
	TArray<FGameplayTag> GetCanUseClassList(const FGameplayTag& ItemTag) const;
	FGameplayTag GetUseKeyTag(const FGameplayTag& ItemTag) const;

	// FItemFeatureData Property Getters
	FText GetItemName(const FGameplayTag& ItemTag) const;
	FText GetHowToInteractText(const FGameplayTag& ItemTag) const;
	FName GetAttachmentSocketName(const FGameplayTag& ItemTag) const;
	int32 GetMaxStack(const FGameplayTag& ItemTag) const;

	// DA가 직접 필요한 경우 (Def로 왠만하면 해결합시다.)
	UItemData* GetItemDataAsset() const { return CachedItemData; }

	// 재료 Map을 던져주면 해당하는 레시피를 찾아주는 헬퍼 함수
	const FItemRecipeData* FindRecipe(const TMap<FGameplayTag, int32>& InputIngredients) const;

private:
	// ItemData 캐시
	UPROPERTY()
	TObjectPtr<UItemData> CachedItemData;

	// DT에서 긁어와 메모리에 올려둘 O(1) 탐색용 캐시 맵
	TMap<FGameplayTag, FItemFeatureData> CachedFeatureData;

	// 레시피 캐시 맵 (Key: 재료의 조합 해시값)C
	TMap<uint32, FItemRecipeData> CachedRecipeData;

	// 재료 TMap을 기반으로 순서에 상관없는 고유 Hash 값을 만들어내는 내부 함수
	uint32 GenerateRecipeHash(const TMap<FGameplayTag, int32>& Ingredients) const;

};