#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "Room/SWRoomSaveGame.h"
#include "ClassFeatureRoomProgressSubsystem.generated.h"

class ABasePlayer;
class ABasePlayerController;

UCLASS()
class CLASSFEATURE_API UClassFeatureRoomProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
 bool IsDevelopmentTransitionBusy() const { return bReturning || bSaving; }
 bool TryDevelopmentFinalDeparture(UWorld* World, ABasePlayerController* Requester, FString& OutError);
	bool TryGameOverRetry(UWorld* World, ABasePlayerController* Requester, uint64 RequestId, FString& OutError);
	bool CaptureControllerProgress(ABasePlayerController* Controller, bool bUseFrozen, FString& OutError);
	bool GetStoredControllerProgress(ABasePlayerController* Controller, FSWRoomPlayerProgress& OutProgress) const;
	void HandleTransitionLogout(ABasePlayerController* Controller);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool RestoreSharedWorld(UWorld* World);
	bool CaptureSharedWorld(UWorld* World);
	void RestorePlayer(ABasePlayer* Player);
	void CapturePlayer(ABasePlayer* Player);
	bool TrySave(UWorld* World, ESWRoomSaveKind Kind, FString& OutError);
	int32 GetLastCaptureIssueCount() const { return LastCaptureIssueCount; }
	bool TryReturn(UWorld* World, ABasePlayer* Requester);
	bool TryFinalDeparture(UWorld* World, ABasePlayer* Requester);
	void ConfirmReturnPresentation(ABasePlayerController* Controller);
private:
 bool TryFinalDepartureInternal(UWorld* World, ABasePlayer* Requester, ABasePlayerController* Controller, bool bDevelopmentTest, FString& OutError);
 bool bDevelopmentFinalDeparture = false;
	void BeginReturnTravel();
	void HandleReturnPresentationTimeout();
	void CancelReturnPresentation();
	UFUNCTION() void HandleGameOverRestart();
	void HandlePostLoadMap(UWorld* World);
	bool TickRestore(float DeltaTime);
	FDelegateHandle PostLoadHandle;
	FTSTicker::FDelegateHandle RestoreTickerHandle;
	TWeakObjectPtr<UWorld> PendingWorld;
	double RestoreDeadline = 0.0;
	bool bReturning = false;
	enum class ERoomTransitionReason : uint8 { Return, FinalDeparture, GameOverRetry };
	ERoomTransitionReason TransitionReason = ERoomTransitionReason::Return;
	int32 FinalDepartureAttemptSerial = 0;
	int32 ActiveFinalDepartureAttemptId = 0;
	TArray<TWeakObjectPtr<ABasePlayerController>> ReturnControllers;
	TArray<TWeakObjectPtr<ABasePlayerController>> PendingReturnControllers;
	FTimerHandle ReturnPresentationTimeoutHandle;
	bool bWorldSnapshotRestored = false;
	bool bFinalDepartureSharedRestored = false;
	bool bReturnShipPlaced = false;
	bool bShipSafetyFallbackUsed = false;
	double ShipSafetyCheckAt = 0.0;
	double ShipPlacementRealTime = 0.0;
	double ShipPlacementWorldTime = 0.0;
	double LastShipSafetyDiagnosticWorldTime = -1.0;
	bool bSaving = false;
	int32 LastCaptureIssueCount = 0;
	void RecordTransitionParticipants(UWorld* World);
	bool AreTransitionParticipantsReady(UWorld* World) const;
	bool ValidateRetryStorage(UWorld* World, FString& OutError) const;
	UPROPERTY(Transient) FSWRoomPlayerProgress RollbackHost;
	UPROPERTY(Transient) TArray<FSWRoomGuestProgress> RollbackGuests;
	UPROPERTY(Transient) FSWRoomSharedProgress RollbackShared;
	UPROPERTY(Transient) bool bRollbackFinalDepartureCompleted = false;
	UPROPERTY(Transient) bool bHasRetryRollback = false;
	TWeakObjectPtr<ABasePlayerController> RetryRequester;
	uint64 RetryRequestId = 0;
	FString RetryCancelReason;
	TSet<TWeakObjectPtr<ABasePlayerController>> FinalPlacedControllers;
	bool bFinalDeparturePlayersPlaced = false;
	bool bTravelAccepted = false;
};
