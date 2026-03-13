#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "BaseItem.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UAbilitySystemComponent;

UCLASS()
class ARTISTICSWCORE_API ABaseItem : public AActor
{
    GENERATED_BODY()

public:
    ABaseItem();

    virtual void Tick(float DeltaTime) override;

    // --- 아이템 데이터 (에디터에서 할당) ---

    // 1. 아이템 종류를 식별하기 위한 Tag (예: Item.Weapon.Bomb, Item.Tool.Hammer)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Data")
    FGameplayTag ItemTag;

    // 2. 주웠을 때 플레이어에게 부여할 스킬 (GAS 어빌리티)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Data")
    TSubclassOf<UGameplayAbility> GrantedAbilityClass;


    // --- 주요 기능 API ---

    // F키를 눌러 줍기 (플레이어 쪽에서 호출)
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    virtual void PickUpItem(AActor* Picker);

    // Q키 등을 눌러 스킬 사용
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    virtual void UseSkill();

protected:
    virtual void BeginPlay() override;

    // --- 컴포넌트 ---
    // VisibleAnywhere로 설정하면, 이 C++을 상속받은 블루프린트 에디터에서 메쉬 모양, 재질 등을 마음대로 바꿀 수 있습니다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
    UStaticMeshComponent* ItemMesh;

    // 상호작용(줍기) 범위를 나타내는 콜리전
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
    USphereComponent* InteractSphere;


    // --- 물리 -> 둥둥 띄우기 로직 ---
    UFUNCTION()
    void OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

    bool bIsHovering;      // 둥둥 떠있는 상태인지 여부
    FVector HoverBaseLoc;  // 둥둥 뜰 때 기준 Z축 위치

private:
    // 플레이어에게 부여한 스킬 핸들 (나중에 아이템을 버릴 때 스킬을 회수하기 위함)
    FGameplayAbilitySpecHandle GrantedAbilityHandle;
};