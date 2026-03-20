// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "GameplayTagContainer.h"
#include "InputTagConfig.h"
#include "BasePlayer.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnAbilitySystemInitializedDelegate);

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;
class ABaseItem;
class UInputTagConfig;

// Item Slot 관리 구조체
USTRUCT(BlueprintType)
struct FItemSlot
{
	GENERATED_BODY()

	// 슬롯에 할당된 GameplayTag (예: key.Item.1)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemSlot")
	FGameplayTag KeyTag;

	// 해당 슬롯에 장착된 아이템 객체 포인터
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemSlot")
	TObjectPtr<ABaseItem> Item;

	FItemSlot(const FGameplayTag& InTag = FGameplayTag::EmptyTag, ABaseItem* InItem = nullptr);

	// Tag로 배열에서 바로 찾기 위한 연산자 오버로딩
	bool operator==(const FGameplayTag& OtherTag) const;

	// Item 포인터로 배열에서 바로 찾기 위한 연산자 오버로딩
	bool operator==(const ABaseItem* OtherItem) const;
};

/**
 * 
 */
UCLASS()
class CLASSFEATURE_API ABasePlayer : public ABaseCharacter
{
	GENERATED_BODY()
public:
	ABasePlayer();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/* --- 네트워크 초기화 ---*/
public:
	// 네트워크 복제 변수 등록
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버에서 빙의될 때 ASC 초기화
	virtual void PossessedBy(AController* NewController) override;

	// 클라이언트에서 PlayerState가 복제 완료되었을 때 ASC 초기화
	virtual void OnRep_PlayerState() override;

	// ASC 초기화 완료를 알리는 델리게이트 (컴포넌트에게 알리기 위함)
	FOnAbilitySystemInitializedDelegate OnAbilitySystemInitialized;

	/* --- 기본 입력 초기화 ---*/
public:
	// 플레이어 입력 바인딩
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// Blueprint에서 호출 가능하도록 노출된 입력 API(필요없으면 지워야 함)
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoLook(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

protected:
	// 서버에 의해 로컬에서 Controller가 조종하는 Pawn이 지정될 때 호출되는 함수.
	virtual void PawnClientRestart() override;

	// Default IMC (마우스 등)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UInputMappingContext> DefaultIMC;

	// Default IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 DefaultIMCPriority = 1;

	// Default IA - Tag 매핑 DA
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputTagConfig> DefaultInputConfig;

	// Input Action
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	// 상호작용 IA (F)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// 마우스 왼클릭 IA 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLeftAction;

	// 마우스 우클릭 IA 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseRightAction;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void Interact();

	// 마우스 왼클릭 시 실행될 함수
	void OnMouseLeftPressed();
	void OnMouseLeftReleased();

	// 마우스 우클릭 시 실행될 함수
	void OnMouseRightPressed();

	/* --- 키 입력으로 실행되는 GA 공통 로직 ---  */
public:
	// GA와 그 GA가 어떤 키 입력(Tag)에 반응할지 함께 적용하는 함수.
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void GrantAbilityToSlot(FGameplayTag SlotTag, TSubclassOf<UGameplayAbility> AbilityClass);

	// 키보드 & 마우스 입력 Tag와 함께 등록된 GA 해제
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void RemoveAbilityFromSlot(FGameplayTag SlotTag);

	// 즉발형 GA에 대해 SlotTag에 매핑된 GA를 실행하는 함수
	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

protected:
	// IA와 Slot Tag의 Mapping 정보가 담긴 DataAsset (BP 주입)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputTagConfig> ItemInputConfig;

	// Item IMC
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UInputMappingContext> ItemIMC;

	// Item IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 ItemIMCPriority = 1;



	/* --- ItemSlot ---  */
public:
	// 현재 캐릭터가 장착하고 있는 아이템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedItem, Category = "Equipment")

	TObjectPtr<ABaseItem> EquippedItem;

	// EquippedItem의 변경이 복제되었을 때 호출되는 함수
	UFUNCTION()
	void OnRep_EquippedItem(ABaseItem* OldItem);

	// ItemSlot 구조체 배열 (복제)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Item")
	TArray<FItemSlot> ItemSlots;

	// 특정 슬롯의 Item을 제거하고 부여된 GA를 회수
	UFUNCTION()
	void RemoveItemFromSlot(FGameplayTag SlotTag);

	UFUNCTION()
	void UseEquippedItem(bool bDestroy = true);

protected:
	// 슬롯 키를 눌렀을 때 아이템을 장착하는 함수
	void EquipItemFromSlot(FGameplayTag SlotTag);

	// 아이템 슬롯에 아이템을 저장하고 장착 상태를 관리
	bool TryPutItemInSlot(ABaseItem* Item);

	/* --- 카메라 ---*/
public:
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	// 평상시 카메라 거리
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float DefaultTargetArmLength = 400.f;

	// 조준 시 카메라 거리
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float AimingTargetArmLength = 150.f;

	// 평상시 카메라 오프셋
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector DefaultSocketOffset = FVector(0.f, 0.f, 0.f);

	// 조준 시 카메라 오프셋
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector AimingSocketOffset = FVector(0.f, 60.f, 50.f);

	// 카메라 전환 보간 속도
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float CameraInterpSpeed = 10.f;

	/* --- Interact를 위한 Trace 범위 ---*/
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractTraceDistance = 500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractTraceRadius = 50.f;
};
