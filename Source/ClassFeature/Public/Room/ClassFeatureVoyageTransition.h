#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Room/SWVoyageResetTypes.h"
#include "Room/SWRoomSaveGame.h"
#include "ClassFeatureVoyageTransition.generated.h"

class UClassFeatureRoomProgressSubsystem;
class ABasePlayerController;
class USWVoyageResetProfile;

/** One game-instance owner for the authority transition and recovery target. */
UCLASS()
class CLASSFEATURE_API UClassFeatureVoyageTransition : public UObject
{
	GENERATED_BODY()
public:
	bool Start(UWorld* World, ESWVoyageReason Reason, bool bDevelopment, FString& OutError);
	bool BeginBootstrap(UWorld* World, FString& OutError);
	bool Tick(float DeltaSeconds);
	void ReceiveAck(ABasePlayerController* Controller, int64 AttemptId, int32 Generation, ESWVoyageAck Ack);
	void HandleLogin(ABasePlayerController* Controller);
	void HandleLogout(ABasePlayerController* Controller);
	void Fail(const FString& Error);
	bool RetryFailure(ABasePlayerController* Controller, int64 AttemptId, int32 Generation);
	bool IsBusy() const { return Context.Phase != ESWVoyagePhase::Idle; }
	bool IsSavingResult() const { return bSavingResult; }
	bool IsRecoveryBootstrap() const { return bRecoveryBootstrap; }
	bool PlaceBootstrapPlayer(ABasePlayerController* Controller, FString& OutError);
	bool Matches(ABasePlayerController* Controller, int64 AttemptId, int32 Generation) const;
	const FSWVoyageResetContext& GetContext() const { return Context; }
	FSWVoyageReplicatedState GetReplicatedState() const;
	void Shutdown();
private:
	struct FParticipant
	{
		TWeakObjectPtr<ABasePlayerController> Controller;
		TWeakObjectPtr<APawn> Pawn;
		FTransform Target = FTransform::Identity;
		FSWRoomPlayerProgress Progress;
		TSet<ESWVoyageAck> Acks;
		double NextSpawnAt = 0.0;
		double NextPlacementAt = 0.0;
		bool bFresh = false;
		bool bPrepared = false;
		bool bPlaced = false;
		bool bLateJoin = false;
	};
	UPROPERTY(Transient) FSWVoyageResetContext Context;
	UPROPERTY(Transient) FSWRoomPlayerProgress TargetHost;
	UPROPERTY(Transient) TArray<FSWRoomGuestProgress> TargetGuests;
	UPROPERTY(Transient) FSWRoomSharedProgress TargetShared;
	UPROPERTY(Transient) FSWRoomPlayerProgress RollbackHost;
	UPROPERTY(Transient) TArray<FSWRoomGuestProgress> RollbackGuests;
	UPROPERTY(Transient) FSWRoomSharedProgress RollbackShared;
	UPROPERTY(Transient) TObjectPtr<USWVoyageResetProfile> Profile;
	TWeakObjectPtr<UWorld> ActiveWorld;
	TArray<FParticipant> Participants;
	FTSTicker::FDelegateHandle TickerHandle;
	int64 AttemptSerial = 0;
	double Deadline = 0.0;
	double TotalDeadline = 0.0;
	bool bCommitted = false;
	bool bDevelopmentDeparture = false;
	bool bStoryCommitAttempted = false;
	bool bRecoveryUsed = false;
	bool bRecoveryPending = false;
	bool bSavingResult = false;
	bool bRollbackFinalCompleted = false;
	bool bSharedApplied = false;
	bool bShipPlaced = false;
	bool bSnapshotCompleted = false;
	bool bSaveSucceeded = false;
	bool bRecoveryBootstrap = false;
	bool bRecoveryContinue = false;
	bool bInitialSaveAttempted = false;
	FString FailureMessage;
	UClassFeatureRoomProgressSubsystem* Owner() const;
	bool InitializeAttempt(UWorld* World, bool bBootstrap, FString& OutError);
	bool Enter(ESWVoyagePhase Phase, FString& OutError);
	void Publish();
	bool HaveAck(ESWVoyageAck Ack) const;
	ESWVoyageStepResult PollRestore(FString& OutError);
	ESWVoyageStepResult PollPlayers(FString& OutError);
	bool ResolvePlacement(int32 Slot, FTransform& OutTransform, FString& OutError) const;
	void StoreTargets();
	bool Recover();
};
