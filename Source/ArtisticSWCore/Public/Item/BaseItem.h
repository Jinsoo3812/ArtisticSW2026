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

    // ==========================================
    // [아이템 데이터] 블루프린트에서 할당할 값들
    // ==========================================

    // 1. 아이템 종류 식별용 태그 (예: Item.Weapon.Grenade)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Data")
    FGameplayTag ItemTag;

    // 2. 이 아이템을 주웠을 때 플레이어에게 부여할 GAS 스킬 (투척 스킬 등)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Data")
    TSubclassOf<UGameplayAbility> GrantedAbilityClass;


    // ==========================================
    // [주요 함수]
    // ==========================================

    // 플레이어가 F키를 눌러서 주울 때 호출됨
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    virtual void PickUpItem(AActor* Picker);

protected:
    virtual void BeginPlay() override;

    // --- 컴포넌트 ---
    // 에디터에서 외형(메쉬)을 바꿀 수 있도록 VisibleAnywhere 설정
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
    UStaticMeshComponent* ItemMesh;

    // F키로 상호작용(줍기)할 수 있는 범위를 나타내는 투명한 구체
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
    USphereComponent* InteractSphere;


    // --- 물리 및 둥둥 뜨기 관련 ---
    // 물리 엔진이 멈췄을 때(Sleep) 호출되어 둥둥 뜨기로 전환하는 함수
    UFUNCTION()
    void OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

    bool bIsHovering;      // 현재 둥둥 떠있는 상태인지?
    FVector HoverBaseLoc;  // 둥둥 뜰 때의 기준 높이(Z) 위치

private:
    // 플레이어에게 부여한 스킬의 식별표 (나중에 버릴 때 스킬을 뺏기 위해 저장해둠)
    FGameplayAbilitySpecHandle GrantedAbilityHandle;
};