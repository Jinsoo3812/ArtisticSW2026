// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "ArtisticSW2026PlayerController.h"
#include "BasePlayerController.generated.h"

/**
 * 
 */

class UInputMappingContext;
class UPlayerHUDWidget;
class UInputAction;
class UInputTagConfig;
class AStorageChest;
class UStorageWindowWidget;

UCLASS()
class CLASSFEATURE_API ABasePlayerController : public AArtisticSW2026PlayerController
{
	GENERATED_BODY()

public:
	/*--- 초기화 ---*/
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/*--- 네트워크 초기화 ---*/
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_Pawn() override;
	
	/*--- UI Input ---*/
protected:
	UPROPERTY(EditAnywhere, Category = "Input")
	TArray<UInputMappingContext*> UIIMC;

	// UI IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 UIIMCPriority = 1;

	UPROPERTY(EditDefaultsOnly,  Category = "Input")
	TObjectPtr<UInputTagConfig> UIInputConfig;

	// UI 입력이 들어왔을 때 실행될 콜백 함수
	void OnUIInputPressed(FGameplayTag InputTag);

	/*---- 인벤토리 ----*/
public:

	void ToggleInventory();
	void OpenStorageFromServer(AStorageChest* StorageChest);

	UFUNCTION(Client, Reliable)
	void ClientOpenStorage(AStorageChest* StorageChest);

	UFUNCTION(Server, Reliable)
	void ServerTransferStorageSlot(AStorageChest* StorageChest, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerHandleStorageLeftClick(AStorageChest* StorageChest, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerQuickMoveInventorySlotToStorage(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerQuickMoveStorageSlotToInventory(AStorageChest* StorageChest, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerCloseStorage(AStorageChest* StorageChest);

	bool HasOpenStorage() const { return ActiveStorageChest != nullptr; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPlayerHUDWidget> PlayerHUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UPlayerHUDWidget> PlayerHUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UStorageWindowWidget> StorageWindowWidgetClass;

	UPROPERTY()
	TObjectPtr<UStorageWindowWidget> StorageWindowWidget;

	UPROPERTY()
	TObjectPtr<AStorageChest> ActiveStorageChest;

	void BindHUDToCurrentPlayer();
	void ApplyInventoryInputMode(bool bOpen);
	void OpenStorage(AStorageChest* StorageChest);
	void CloseStorage(bool bNotifyServer = true);
	bool IsStorageOpen() const;
	
};
