#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "Room/SWRoomSaveGame.h"
#include "Room/SWVoyageResetTypes.h"
#include "Room/SWVoyageResetParticipant.h"
#include "ClassFeatureRoomProgressSubsystem.generated.h"

class ABasePlayer;
class ABasePlayerController;
class UClassFeatureVoyageTransition;

UCLASS()
class CLASSFEATURE_API UClassFeatureRoomProgressSubsystem : public UGameInstanceSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()
public:
 virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::Preserve; }
 virtual FName GetVoyageParticipantId_Implementation() const override;
 bool StartVoyageTransition(UWorld* World, ESWVoyageReason Reason, bool bDevelopment, FString& OutError);
 ESWVoyageStepResult PollInPlaceRestore(FString& OutError);
 void HandleVoyageStageAck(ABasePlayerController* Controller, int64 AttemptId, int32 Generation, ESWVoyageAck Ack);
 void HandleVoyageParticipantLogin(ABasePlayerController* Controller);
 void HandleVoyageParticipantLogout(ABasePlayerController* Controller);
 bool RetryVoyageFailure(ABasePlayerController* Controller, int64 AttemptId, int32 Generation);
 bool IsInPlaceVoyageBusy() const;
 void ReportVoyageFailure(ABasePlayerController* Controller, int64 AttemptId, int32 Generation, const FString& Error);
 bool IsDevelopmentTransitionBusy() const { return bSaving || IsInPlaceVoyageBusy(); }
 bool TryDevelopmentFinalDeparture(UWorld* World, ABasePlayerController* Requester, FString& OutError);
 bool ExecuteDevelopmentVoyageProbe(ABasePlayerController* Requester, const FString& Command, const FString& Phase, float Seconds, int64 ExpectedAttempt, int32 ExpectedGeneration, FString& OutError);
	bool TryGameOverRetry(UWorld* World, ABasePlayerController* Requester, uint64 RequestId, FString& OutError);
	bool CaptureControllerProgress(ABasePlayerController* Controller, bool bUseFrozen, FString& OutError);
	bool GetStoredControllerProgress(ABasePlayerController* Controller, FSWRoomPlayerProgress& OutProgress) const;
	void HandleTransitionLogout(ABasePlayerController* Controller);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool RestoreSharedWorld(UWorld* World);
	bool RestoreStoryProgress(UWorld* World, const FSWRoomSharedProgress& Target, FString& OutError);
	ESWVoyageStepResult RestoreSharedActors(UWorld* World, const FSWRoomSharedProgress& Target, FString& OutError);
	bool CaptureSharedWorld(UWorld* World);
	void RestorePlayer(ABasePlayer* Player);
	void CapturePlayer(ABasePlayer* Player);
	bool TrySave(UWorld* World, ESWRoomSaveKind Kind, FString& OutError);
	int32 GetLastCaptureIssueCount() const { return LastCaptureIssueCount; }
	bool TryReturn(UWorld* World, ABasePlayer* Requester);
	bool TryFinalDeparture(UWorld* World, ABasePlayer* Requester);
	void ConfirmReturnPresentation(ABasePlayerController* Controller);
private:
 friend class UClassFeatureVoyageTransition;
 UPROPERTY(Transient) TObjectPtr<UClassFeatureVoyageTransition> VoyageTransition;
 bool PlaceVoyageShip(UWorld* World, bool bFinal, bool bContinue, FString& OutError);
	static void NormalizePlayerForVoyage(FSWRoomPlayerProgress& Progress);
	TWeakObjectPtr<UWorld> SharedRestoreWorld;
	int32 SharedRestoreGeneration = -1;
	bool bVoyageSharedUpgradeRestored = false;
	TMap<FString, TWeakObjectPtr<AActor>> RestoredSharedChests;
 bool TryFinalDepartureInternal(UWorld* World, ABasePlayer* Requester, ABasePlayerController* Controller, bool bDevelopmentTest, FString& OutError);
 UFUNCTION() void HandleGameOverRestart();
 void HandlePostLoadMap(UWorld* World);
 FDelegateHandle PostLoadHandle;
	bool bSaving = false;
	int32 LastCaptureIssueCount = 0;
	TWeakObjectPtr<ABasePlayerController> RetryRequester;
	uint64 RetryRequestId = 0;
	bool ValidateRetryStorage(UWorld* World, FString& OutError) const;

};
