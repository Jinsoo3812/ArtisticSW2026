#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "ItemData.generated.h"

class UStaticMesh;
class ABaseProjectile;

// 아이템 하나에 대한 정의 구조체
USTRUCT(BlueprintType)
struct FItemDefinition
{
	GENERATED_BODY()

	/*
	* TSubclassOf : A 클래스가 B 클래스를 TSubclassOf로 들고 있다면, A 클래스 객체가 로드될 때 B도 같이 로드된다.
	* TSoftObjectPtr : A 클래스가 B 클래스를 TSoftClassPtr로 들고 있다면, B는 실제로 사용될 때 로드된다. (초기 로딩 감소)
	*/

	// 아이템의 이름 (For UI)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText ItemName;

	// 아이템의 사용법 (For UI)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText HowToInteractText;

	// 아이템의 외형
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UStaticMesh> ItemMesh;

	// 주웠을 때 플레이어에게 부여할 어빌리티 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftClassPtr<UGameplayAbility> GrantedAbilityClass;

	// 실제 사용 시 스폰할 실제 액터 (예: BombProjectile)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftClassPtr<AActor> SpawnClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FName AttachmentSocketName = FName("GripPoint");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TArray<FGameplayTag> CanUseClassList;

	// 필요하다면 아이템 이름, 아이콘(UI용 UTexture2D) 등도 여기에 추가
};

// Item의 식별 Tag, Mesh, GA, SpawnClass 등을 한 곳에서 관리하는 DA
UCLASS(BlueprintType, Const)
class ARTISTICSWCORE_API UItemData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ItemData는 주로 Tag로 접근하므로 Map 관리
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Data")
	TMap<FGameplayTag, FItemDefinition> ItemDefinitions;

	// 외부에서 태그로 쉽게 구조체 포인터를 얻어갈 수 있는 헬퍼 함수
	const FItemDefinition* FindItemDefinition(const FGameplayTag& ItemTag) const
	{
		return ItemDefinitions.Find(ItemTag);
	}
};