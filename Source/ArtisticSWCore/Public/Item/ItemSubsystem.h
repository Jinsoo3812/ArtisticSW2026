#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "BaseItem.h"
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

	//ItemData를 직접 사용하지 않고 Def 구조체만 사용하는 대부분의 경우
	const FItemDefinition* GetItemDefinition(const FGameplayTag& ItemTag) const;

	// DA가 직접 필요한 경우 (Def로 왠만하면 해결합시다.)
	UItemData* GetItemDataAsset() const { return CachedItemData; }

private:
	// ItemData 캐시
	UPROPERTY()
	TObjectPtr<UItemData> CachedItemData;

	void LoadItemData();
};