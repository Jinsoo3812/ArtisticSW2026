// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Inventory/InventoryComponent.h"
#include "TimerManager.h"
#include "Upgrade/ShipUpgradeTypes.h"
#include "ArtisticSW2026PlayerController.h"
#include "Engine/GameViewportDelegates.h"
#include "Respawn/SWRespawnControllerInterface.h"
#include "Room/SWRoomSaveGame.h"
#include "Camera/CameraTypes.h"
#include "BasePlayerController.generated.h"

/**
 * 
 */

class ABasePlayer;
class UInputMappingContext;
class UPlayerHUDWidget;
class UInputAction;
class UInputTagConfig;
class AStorageChest;
class UStorageWindowWidget;
class UFacilityHubWidget;
class UStatusWindowWidget;
class USWRoomMenuWidget;
class AFacilityHubActor;
class ASharedShipUpgradeState;
class UGameViewportClient;

struct FStorageRevealState
{
	int32 RevealedSlotCount = 0;
	int32 SearchingSlotIndex = INDEX_NONE;
};

UCLASS()
class CLASSFEATURE_API ABasePlayerController : public AArtisticSW2026PlayerController, public ISWRespawnControllerInterface
{
	GENERATED_BODY()

public:
	ABasePlayerController();
	bool IsDevelopmentTestInputBlockedByUI() const;
	bool IsDevelopmentTestInputBlockedByServerUI() const;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USWDevTestInputComponent> DevTestInput;
	virtual bool CaptureLatestLifeProgress(APawn* SourcePawn) override;
	virtual void SetDeathFlowState(const FSWDeathFlowState& State) override;
	virtual bool HasPendingLifeProgress() const override { return bHasLatestLifeProgress; }
	virtual bool ApplyPendingLifeProgress(APawn* NewPawn) override;
	virtual bool WasLastLifeProgressApplySuccessful(APawn* NewPawn) const override;
	virtual void FreezeLifeProgressForGameOver() override;
	virtual void ReleaseFrozenLifeProgress() override;
	bool GetLatestLifeProgress(FSWRoomPlayerProgress& OutProgress) const;
	ABasePlayer* GetLifeCharacter() const;
	bool IsLifeCharacterAlive() const;
	bool CanMutateGameplay() const;
	bool CleanupLifeInteraction();
	void RequestGameOverRetry();
	UFUNCTION(Server, Reliable) void ServerRequestGameOverRetry(int32 ExpectedRestoreGeneration, uint64 RequestId);
	UFUNCTION(Client, Reliable) void ClientGameOverRetryResult(uint64 RequestId, bool bAccepted, const FString& Message);
	UFUNCTION(Client, Reliable) void ClientSetCameraPublishEnabled(bool bEnabled, int32 RestoreGeneration, int32 ObservationGeneration);
	UFUNCTION(Server, Unreliable) void ServerPublishObservedCamera(const FSWObservedCameraFrame& Frame);
	UFUNCTION(Client, Unreliable) void ClientReceiveObservedCamera(const FSWObservedCameraFrame& Frame);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(ReplicatedUsing=OnRep_DeathFlowState) FSWDeathFlowState DeathFlowState;
	UFUNCTION() void OnRep_DeathFlowState();
	UPROPERTY(Transient) FSWRoomPlayerProgress LatestLifeProgress;
	UPROPERTY(Transient) bool bHasLatestLifeProgress = false;
	UPROPERTY(Transient) bool bLifeProgressFrozen = false;
	UPROPERTY(Transient) bool bPendingLifeProgressApplied = false;
	bool bGameOverReconnect = false;
	TWeakObjectPtr<ABasePlayer> LifeCharacter;
	TWeakObjectPtr<APawn> AppliedLifePawn;
private:
	void TickDeathFlow(float DeltaTime);
	void ApplyLocalDeathFlow();
	UPROPERTY(Transient) TObjectPtr<class USWDeathFlowWidget> DeathFlowWidget;
	UPROPERTY(Transient) TObjectPtr<class ACameraActor> DeathCamera;
	ESWSessionLifePhase LocalSessionPhase = ESWSessionLifePhase::Playing;
	bool bDeathInputLocked = false;
	bool bDeathFlowInputModeApplied = false;
	bool bDeathFlowGameOverInput = false;
	bool bRetryFocusApplied = false;
	bool bGameOverCharacterProtected = false;
	bool bLifeCharacterCouldBeDamaged = true;
	bool bLifeCharacterWasInvulnerable = false;
	bool bSavedAutoCamera = true;
	bool bCameraPublishing = false;
	bool bHasOwnPOV = false;
	bool bHasObservedPOV = false;
	FMinimalViewInfo LastOwnAlivePOV;
	FMinimalViewInfo FrozenOwnDeathPOV;
	int32 PublishRestoreGeneration = 0;
	int32 PublishObservationGeneration = 0;
	uint32 CameraSequence = 0;
	uint32 LastAcceptedSequence = 0;
	uint32 LastObservedSequence = 0;
	int32 LocalObservationGeneration = -1;
	TWeakObjectPtr<APlayerState> LocalObservedPlayerState;
	double LastPublishTime = -1;
	double LastReceiveTime = -1;
	double LastServerCameraTime = -1;
	double LastSpectatorRefreshTime = -1;
	double LastDeathFlowDiagnosticTime = -1;
	uint64 NextRetryRequestId = 0;
	uint64 PendingRetryRequestId = 0;
	uint64 LastRetryRequestId = 0;
	bool bLastRetryAccepted = false;
	FString LastRetryMessage;
	FString RetryStatus;
public:
	void ReportGameOverRetryResult(uint64 RequestId, bool bAccepted, const FString& Message);
	void RequestRoomSave();
	void RequestRoomSaveAndExit();
	void CloseRoomMenu();
	UFUNCTION(Server, Reliable) void ServerRequestRoomSave(uint64 RequestId);
	UFUNCTION(Client, Reliable) void ClientRoomSaveResult(uint64 RequestId, bool bSuccess, const FString& Message);
	UFUNCTION(Client, Reliable) void ClientBeginRoomReturn();
	UFUNCTION(Client, Reliable) void ClientBeginFinalDeparture(int32 AttemptId);
	UFUNCTION(Client, Reliable) void ClientCancelRoomReturn();
	UFUNCTION(Server, Reliable) void ServerConfirmRoomReturnPresentation();
	void OpenFacilityHubFromServer(AActor* ContextActor);

	UFUNCTION(Client, Reliable)
	void ClientOpenFacilityHub(AActor* ContextActor);

	UFUNCTION(BlueprintCallable, Category = "Facility Hub")
	void CloseFacilityHub();

	UFUNCTION(BlueprintPure, Category = "Facility Hub")
	bool IsFacilityHubOpen() const;

	UFUNCTION(Server, Reliable)
	void ServerReleaseFacilityHub(AFacilityHubActor* FacilityHub);

	UFUNCTION(Server, Reliable)
	void ServerRequestActivateSharedShipUpgrade(ASharedShipUpgradeState* SharedState, FName NodeId);

	UFUNCTION(Client, Reliable)
	void ClientReceiveSharedShipUpgradeResult(
		ASharedShipUpgradeState* SharedState,
		FName NodeId,
		EShipUpgradeActivationResult Result,
		const FText& Message);

	/*--- 초기화 ---*/
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
	void CloseStorageFromServer(AStorageChest* StorageChest);

	UFUNCTION(Client, Reliable)
	void ClientOpenStorage(AStorageChest* StorageChest);

	UFUNCTION(Client, Reliable)
	void ClientCloseStorage(AStorageChest* StorageChest);

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

	UFUNCTION(Server, Reliable)
	void ServerSharedStorageSlotAction(AStorageChest* Chest, int32 Index, FGameplayTag ExpectedTag, int32 ExpectedCount, int32 ExpectedCapacity, bool bQuickMove);
	UFUNCTION(Server, Reliable)
	void ServerQuickMoveInventorySlotInTab(EInventoryTab Tab, int32 Index, FGameplayTag ExpectedTag, int32 ExpectedCount);
	bool CanAccessStorage(AStorageChest* Chest) const;
	bool HasOpenStorage() const { return ActiveStorageChest != nullptr; }
	/** Consume F while a chest or facility window is open, regardless of overlap. */
	bool CloseActiveInteractionWindow();
	bool IsStorageSlotRevealed(AStorageChest* StorageChest, int32 SlotIndex) const;
	bool IsStorageSlotSearching(AStorageChest* StorageChest, int32 SlotIndex) const;

protected:
	/** Assign WBP_WorkspaceScreen. It is the one shared shell for every facility tab. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Facility Hub")
	TSubclassOf<UFacilityHubWidget> FacilityHubWidgetClass;

	UPROPERTY()
	TObjectPtr<UFacilityHubWidget> FacilityHubWidget;

	UPROPERTY()
	TObjectPtr<AFacilityHubActor> ActiveFacilityHub;

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
	bool bInteractionMovementLocked = false;

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
	UPROPERTY(Transient) TObjectPtr<UInputAction> RoomMenuAction;
	UPROPERTY(Transient) TObjectPtr<USWRoomMenuWidget> RoomMenuWidget;
	bool bRoomSavePending = false;
	bool bExitAfterRoomSave = false;
	uint64 NextRoomSaveRequestId = 0;
	uint64 PendingRoomSaveRequestId = 0;
	uint64 LastServerRoomSaveRequestId = 0;
	bool bLastServerRoomSaveSuccess = false;
	FString LastServerRoomSaveMessage;
	FTimerHandle RoomSaveTimeoutHandle;
	FOnWindowCloseRequested PreviousWindowCloseRequested;
	TWeakObjectPtr<UGameViewportClient> BoundRoomViewport;
	bool HandleRoomWindowCloseRequested();
	void HandleRoomSaveTimeout();
	bool bCursorVisibleBeforeRoomMenu = false;
	void ApplyInventoryInputMode(bool bOpen);
	void UpdateInteractionMovementLock();
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
