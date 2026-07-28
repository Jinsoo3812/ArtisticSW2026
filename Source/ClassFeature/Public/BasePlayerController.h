// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
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
class UFacilityHubWidget;
class UStatusWindowWidget;

struct FStorageRevealState
{
	int32 RevealedSlotCount = 0;
	int32 SearchingSlotIndex = INDEX_NONE;
};

UCLASS()
class CLASSFEATURE_API ABasePlayerController : public AArtisticSW2026PlayerController
{
	GENERATED_BODY()

public:
	void OpenFacilityHubFromServer(AActor* ContextActor);

	UFUNCTION(Client, Reliable)
	void ClientOpenFacilityHub(AActor* ContextActor);

	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void CloseFacilityHub();

	UFUNCTION(BlueprintPure, Category = "Facility Hub")
	bool IsFacilityHubOpen() const;

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
	void ToggleStatus();

	void OpenStorageFromServer(AStorageChest* StorageChest);

	UFUNCTION(Client, Reliable)
	void ClientOpenStorage(AStorageChest* StorageChest);

	UFUNCTION(Client, Reliable)
	void ClientUpdateStorageRevealState(AStorageChest* StorageChest, int32 RevealedSlotCount, int32 SearchingSlotIndex);

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
	bool IsStorageSlotRevealed(AStorageChest* StorageChest, int32 SlotIndex) const;
	bool IsStorageSlotSearching(AStorageChest* StorageChest, int32 SlotIndex) const;

protected:
	/** Assign WBP_WorkspaceScreen. It is the one shared shell for every facility tab. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Facility Hub")
	TSubclassOf<UFacilityHubWidget> FacilityHubWidgetClass;

	UPROPERTY()
	TObjectPtr<UFacilityHubWidget> FacilityHubWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPlayerHUDWidget> PlayerHUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UPlayerHUDWidget> PlayerHUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UStatusWindowWidget> StatusWindowWidgetClass;

	UPROPERTY()
	TObjectPtr<UStatusWindowWidget> StatusWindowWidget;

	ESlateVisibility PlayerHUDVisibilityBeforeStatus = ESlateVisibility::Visible;
	ESlateVisibility PlayerHUDVisibilityBeforeFacilityHub = ESlateVisibility::Visible;
	TWeakObjectPtr<APawn> StatusInputLockedPawn;
	bool bWasStatusPawnInputEnabled = true;
	bool bStatusCharacterInputLocked = false;
	bool bInventoryInputModeApplied = false;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UStorageWindowWidget> StorageWindowWidgetClass;

	UPROPERTY()
	TObjectPtr<UStorageWindowWidget> StorageWindowWidget;

	UPROPERTY()
	TObjectPtr<AStorageChest> ActiveStorageChest;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search", meta = (ClampMin = "0.0"))
	float CommonSearchTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search", meta = (ClampMin = "0.0"))
	float RelicSearchTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search", meta = (ClampMin = "0.0"))
	float RareSearchTime = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search", meta = (ClampMin = "0.0"))
	float EpicSearchTime = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Storage|Search", meta = (ClampMin = "0.0"))
	float LegendarySearchTime = 3.0f;

	TMap<AStorageChest*, FStorageRevealState> StorageRevealStates;
	FTimerHandle StorageSearchTimerHandle;

	void BindHUDToCurrentPlayer();
	void HandleMenuEscape();
	void ApplyInventoryInputMode(bool bOpen);
	void SetStatusCharacterInputLocked(bool bLocked);
	void OpenStorage(AStorageChest* StorageChest);
	void CloseStorage(bool bNotifyServer = true);
	bool IsStorageOpen() const;
	void StartStorageSearch(AStorageChest* StorageChest);
	void RevealCurrentStorageSlot();
	void NotifyStorageRevealState(AStorageChest* StorageChest);
	int32 FindNextUnrevealedStorageSlot(AStorageChest* StorageChest) const;
	float GetStorageSlotSearchTime(AStorageChest* StorageChest, int32 SlotIndex) const;
	
};
