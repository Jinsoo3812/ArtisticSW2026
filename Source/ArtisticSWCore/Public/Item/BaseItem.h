#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "BaseItem.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UItemData;
struct FItemDefinition;

UCLASS()
class ARTISTICSWCORE_API ABaseItem : public AActor
{
    GENERATED_BODY()

public:
	ABaseItem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/* 네트워크 설정 */
public:
	// 복제할 멤버 변수 설정
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	virtual void OnRep_ItemTag();

protected:
	// BeginPlay 및 OnRep으로 Tag가 도착했을 때 Item 초기화 함수
	void InitializeItem();

	/* Item 핵심 멤버 */
public:
	// Item 식별 Gameplay Tag
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_ItemTag, Category = "Item|Data")
	FGameplayTag ItemTag;

	// DA로부터 가져온 본인의 정의 구조체
	const FItemDefinition* MyDefinition;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
	UStaticMeshComponent* ItemMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
	USphereComponent* InteractSphere;

	// ItemData DA 캐시
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Data")
	TObjectPtr<UItemData> ItemDataAsset;

	/* API for Player */
public:
	// Player가 Item을 주워 자신의 손/ItemSlot/Inventory에 저장하기 위한 함수.
	UFUNCTION(BlueprintCallable, Category = "Item|Action")
	virtual void PickUpItem(AActor* Picker);

	// Item이 부여하는 GA Class 반환 함수
	UFUNCTION(BlueprintCallable, Category = "Item|Data")
	TSubclassOf<UGameplayAbility> GetGrantedAbilityClass() const;

	UFUNCTION(BlueprintCallable, Category = "Item|Data")
	TSubclassOf<AActor> GetSpawnClass() const;

	UFUNCTION(BlueprintCallable, Category = "Item|Data")
	UStaticMesh* GetStaticMesh() const;

	UFUNCTION(BlueprintCallable, Category = "Item|Data")
	TArray<FGameplayTag> GetCanUseAbilityList() const;

	// 던져졌을 때 물리 및 충돌 상태를 복구하는 함수
	UFUNCTION(BlueprintCallable, Category = "Item|Action")
	virtual void OnThrown(FVector LaunchVelocity, AActor* Thrower);

	/* Hovering */
protected:
	bool bIsHovering;

	FVector HoverBaseLoc;

	UPROPERTY(EditDefaultsOnly, Category = "Hover")
	float HoverHeight = 40.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hover")
	float HoverSpeed = 45.f;

	// 물리 연산이 꺼진 상태에 발동되어 Hovering 시작
	UFUNCTION()
	void OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);
};