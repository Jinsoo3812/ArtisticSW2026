// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "CrafterComponent.generated.h"

class UGameplayAbility;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UInputTagConfig;
class ABasePlayer;
class ABaseItem;
struct FGameplayEventData;
class UUserWidget;
class AWorkTable;
class UItemData;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CLASSFEATURE_API UCrafterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrafterComponent();

protected:
	virtual void BeginPlay() override;

	// ASC 초기화 완료 시 호출될 Crafter 시스템 초기화 함수
	void SetupCrafterSystem();

	/* --- IMC 추가 및 입력 바인딩 --- */
public:
	// Crafter 전용 입력 매핑 컨텍스트
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Input")
	UInputMappingContext* CrafterIMC;

	// IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Input")
	int32 CrafterIMCPriority = 2;

	// Crafter 전용 입력 설정 (IA <-> Tag)
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Input")
	UInputTagConfig* CrafterInputConfig;

	// Player의 SetupPlayerInputComponent 시점에 호출하여 Crafter 전용 입력을 바인딩
	void BindCrafterInput(UEnhancedInputComponent* EnhancedInputComponent);

	// Player에게 Crafter 전용 IMC를 추가/제거
	void AddCrafterMappingContext();
	void RemoveCrafterMappingContext();


	/* --- Player에게 부여하는 GA 관리 --- */
protected:
	// 서버에서 어빌리티를 부여할 때 호출합니다.
	void GrantCrafterAbilities();

	// 컴포넌트 제거 시 부여된 어빌리티를 회수합니다.
	void RemoveCrafterAbilities();

	// Crafter 전용 어빌리티와 고정 Slot Tag의 매핑 정보 (ex. Crafer가 되면 R은 무조건 투척 GA)
	UPROPERTY(EditDefaultsOnly, Category = "Crafter|Abilities")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> CrafterAbilities;


	/* --- WorkTable 관리 --- */
public:
	// 서버로 제작 완료를 알리는 RPC
	UFUNCTION(Server, Reliable)
	void Server_CompleteCrafting(AWorkTable* TargetTable);
	

protected:
	// ItemData DA 캐시
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafter|WorkTable")
	TObjectPtr<UItemData> ItemDataAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Crafter|WorkTable")
	UInputMappingContext* WorkTableIMC;

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveCraftingUI;

	// 현재 상호작용 중인 작업대를 기억할 캐시 변수
	UPROPERTY()
	TWeakObjectPtr<AWorkTable> CurrentInteractingTable;

	UPROPERTY(EditDefaultsOnly, Category = "Crafter|WorkTable")
	float WorkTableIMCPriority = 10;

	// 스타포스 진행상태 추적
	bool bIsPlayingStarforce = false;

	// WorkTable UI를 띄우는 함수
	void ShowCraftingUI(AActor* TargetActor);

	// 서버가 클라이언트에게 호출하는 RPC (WorkTable UI를 띄우라고)
	UFUNCTION(Client, Reliable)
	void ClientRPC_OpenCraftingUI(AActor* TargetActor);

	// WorkTable과의 상호작용 이벤트 처리(서버/클라 분리) 함수
	void HandleCraftEvent(const FGameplayEventData* Payload);

	// ESC 입력 시 UI를 닫고 입력을 되돌리는 함수
	void EndCrafting();

	// 스페이스바 입력 시 호출될 함수 (스타포스 플레이)
	UFUNCTION(BlueprintCallable, Category = "Interaction|Craft")
	void SpaceBarAction();

	// 작업대 UI로부터의 StarForce 성공 이벤트를 구독할 함수
	UFUNCTION()
	void HandleStarForceSuccess();
};
