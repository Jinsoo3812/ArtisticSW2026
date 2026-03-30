#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "Interactable.h"
#include "BaseGameplayTags.h"
#include "BaseItem.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UItemData;
struct FItemDefinition;
class UInteractableComponent;
class UTexture2D;
class UItemData;

UENUM(BlueprintType)
enum class EItemState : uint8
{
	Dropped_Simulating, // 땅에 던져져서 물리 연산 중인 상태
	Dropped_Hovering,   // 물리 연산이 끝나고 둥둥 떠있는 상태
	Equipped,           // 플레이어 손에 장착된 상태 (숨김 해제, 콜리전/물리 꺼짐)
	InItemSlot         // 인벤토리에 들어간 상태 (숨김, 콜리전/물리 꺼짐)
};

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
	void OnRep_ItemTag();

	UFUNCTION()
	void OnRep_ItemState();

protected:
	// BeginPlay 및 OnRep으로 Tag가 도착했을 때 Item 초기화 함수
	void InitializeItem();

	/* Item 핵심 멤버 */
public:
	// Item 식별 Gameplay Tag
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_ItemTag, Category = "Item|Data")
	FGameplayTag ItemTag;

	// 서브시스템에서 ItemDefinition을 가져오기
	const FItemDefinition* GetDefinitionFromSubsystem() const;

	// DA로부터 가져온 본인의 정의 구조체
	const FItemDefinition* MyDefinition;

	void SetItemState(EItemState NewState);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
	UStaticMeshComponent* ItemMesh;

	// Interact Interface를 통한 상호작용 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Components")
	UInteractableComponent* InteractableComponent;

	UPROPERTY(ReplicatedUsing = OnRep_ItemState)
	EItemState ItemState;

	// 상호작용 컴포넌트의 OnInteracted 방송을 들었을 때 실행될 콜백 함수
	UFUNCTION()
	void OnInteractableTriggered(AActor* Interactor);

	/* API for Player */
public:
	// Player가 Item을 주워 자신의 손/ItemSlot/Inventory에 저장하기 위한 함수.
	// Interact 함수에서 호출되며 캡슐화되어야 하지만 PR이후 리팩토링 고려
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
	FVector HoverBaseLoc;

	UPROPERTY(EditDefaultsOnly, Category = "Hover")
	float HoverHeight = 40.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hover")
	float HoverSpeed = 45.f;

	// 물리 연산이 꺼진 상태에 발동되어 Hovering 시작
	UFUNCTION()
	void OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

public:
	// DA에서 아이콘 Getter
	UTexture2D* GetItemIcon() const;
	// DA에서 이름 Getter
	FText GetItemNameText() const;
};