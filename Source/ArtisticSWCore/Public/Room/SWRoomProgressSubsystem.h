#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Room/SWRoomSaveGame.h"
#include "SWRoomProgressSubsystem.generated.h"

class USWRoomSaveGame;

USTRUCT()
struct FSWExpectedTransitionPlayer
{
	GENERATED_BODY()
	UPROPERTY() int32 Slot = INDEX_NONE;
	UPROPERTY() FString GuestKey;
};

UCLASS()
class ARTISTICSWCORE_API USWRoomProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	bool IsDevelopmentTestSessionEnabled(UWorld* World) const;
	void SetDevelopmentTestSessionEnabled(UWorld* World, bool bEnabled);
	void SetDevelopmentFinalDeparturePending(UWorld* World, bool bPending, bool bValidatedTransition = false);
	bool ConsumeDevelopmentFinalDeparturePending(UWorld* World);
	bool IsDevelopmentFinalEncounterWorld(UWorld* World) const;
	void ClearDevelopmentFinalEncounterWorld(UWorld* World);
	virtual void Deinitialize() override;
	void MarkGameOverRetryTravelPending() { ClearDevelopmentFinalEncounterWorld(nullptr); bGameOverRetryTravelPending = true; }
	void ClearGameOverRetryTravelPending() { bGameOverRetryTravelPending = false; }
	bool IsGameOverRetryTravelPending() const { return bGameOverRetryTravelPending; }
	UPROPERTY(Transient) TArray<FSWExpectedTransitionPlayer> ExpectedTransitionPlayers;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	bool IsHostedRoom() const { return bHostedRoom; }
	const USWRoomSaveGame* GetActiveRoom() const { return ActiveRoom; }
	USWRoomSaveGame* GetMutableActiveRoom() { return ActiveRoom; }
	bool WriteCheckpoint();
	bool IsNewRoomPending() const { return bNewRoomPending; }
	bool IsNewRoomCommitted() const { return bNewRoomCommitted; }
	bool HasStartupError() const { return bStartupError; }
	bool BeginWorldRecovery();
	bool IsWorldRecoveryPending() const { return bWorldRecoveryPending; }
	bool WasWorldRecovered() const { return bWorldRecoveryAttempted; }
	void CompleteWorldRecovery() { bWorldRecoveryPending = false; }
	FGuid GetHostKey() const { return HostKey; }
	int32 AdvanceRestoreGeneration() { return ++RestoreGeneration; }
	int32 GetRestoreGeneration() const { return RestoreGeneration; }
	void MarkReturnTravelPending() { ClearDevelopmentFinalEncounterWorld(nullptr); bReturnTravelPending = true; }
	void ClearReturnTravelPending() { bReturnTravelPending = false; }
	bool IsReturnTravelPending() const { return bReturnTravelPending; }
	void MarkFinalDepartureTravelPending() { bFinalDepartureTravelPending = true; }
	void ClearFinalDepartureTravelPending() { bFinalDepartureTravelPending = false; }
	bool IsFinalDepartureTravelPending() const { return bFinalDepartureTravelPending; }
	void MarkGameOverTravelPending() { ClearDevelopmentFinalEncounterWorld(nullptr); bGameOverTravelPending = true; }
	void ClearGameOverTravelPending() { bGameOverTravelPending = false; }
	bool IsGameOverTravelPending() const { return bGameOverTravelPending; }
private:
	friend class FRoomWorldRecoveryPolicyTest;
	TWeakObjectPtr<UWorld> DevelopmentSessionWorld;
	TWeakObjectPtr<UWorld> DevelopmentEncounterWorld;
	bool bDevelopmentSessionEnabled = false;
	bool bDevelopmentFinalDeparturePending = false;
	FString DevelopmentFinalTargetPackage;
	UPROPERTY(Transient) TObjectPtr<USWRoomSaveGame> ActiveRoom;
	FGuid HostKey;
	bool bHostedRoom = false;
	bool bStartupError = false;
	bool bWorldRecoveryPending = false;
	bool bWorldRecoveryAttempted = false;
	bool bReturnTravelPending = false;
	bool bFinalDepartureTravelPending = false;
	bool bGameOverTravelPending = false;
	UPROPERTY(Transient) bool bGameOverRetryTravelPending = false;
	bool bNewRoomPending = false;
	bool bNewRoomCommitted = false;
	bool bSaving = false;
	int32 RestoreGeneration = 0;
};
