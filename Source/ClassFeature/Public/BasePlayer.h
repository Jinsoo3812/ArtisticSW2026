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

	// Item IMC
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UInputMappingContext> ItemIMC;

	// Item IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 ItemIMCPriority = 1;

	// Input Action
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// 마우스 왼클릭 IA 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLeftAction;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void Interact();

	// 마우스 왼클릭 시 실행될 함수
	void OnMouseLeftPressed();
	void OnMouseLeftReleased();

	/* --- 키 입력으로 실행되는 GA 공통 로직 ---  */
public:
	// 특정 슬롯에 GA를 부여하는 함수
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void GrantAbilityToSlot(FGameplayTag SlotTag, TSubclassOf<UGameplayAbility> AbilityClass);

	// 특정 슬롯에서 GA를 회수하는 함수
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void RemoveAbilityFromSlot(FGameplayTag SlotTag);

	// 즉발형 GA에 대해 SlotTag에 매핑된 GA를 실행하는 함수
	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

protected:
	// IA와 Slot Tag의 Mapping 정보가 담긴 DataAsset (BP 주입)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputTagConfig> SlotInputConfig;



	/* --- ItemSlot ---  */
public:
	// 현재 캐릭터가 장착하고 있는 아이템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")

	TWeakObjectPtr<ABaseItem> EquippedItem;

	// 소유하고 있는 Item 배열
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<TObjectPtr<ABaseItem>> ItemSlots;

	// 특정 슬롯의 Item을 제거하고 부여된 GA를 회수
	UFUNCTION()
	void RemoveItemFromSlot(FGameplayTag SlotTag);

	UFUNCTION()
	void UseEquippedItem();

protected:
	// ItemSlot Tag에 해당하는 ItemSlot index 매핑
	UPROPERTY(Transient)
	TMap<FGameplayTag, int32> ItemSlotTagToIndexMap;

	// ItemSlot index에 해당하는 ItemSlot Tag 매핑
	UPROPERTY(Transient)
	TArray<FGameplayTag> IndexToItemSlotTagArray;

	// Slot Tag를 ItemSlot index로 변환
	int32 GetItemSlotIndexByTag(const FGameplayTag& SlotTag) const;

	// 슬롯 키를 눌렀을 때 아이템을 장착하는 함수
	void EquipItemFromSlot(FGameplayTag SlotTag);

	/* --- 카메라 ---*/
public:
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;
};
