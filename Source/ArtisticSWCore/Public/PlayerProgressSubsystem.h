#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlayerRespawnTypes.h"
#include "Room/SWVoyageResetParticipant.h"
#include "PlayerProgressSubsystem.generated.h"

/** Transient bridge that survives an OpenLevel and preserves per-player run progress. */
UCLASS()
class ARTISTICSWCORE_API UPlayerProgressSubsystem : public UGameInstanceSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	void ClearSnapshot(int32 PlayerIndex) { PendingSnapshots.Remove(PlayerIndex); }
	void ClearReconnectSnapshot(const FString& PlayerKey) { ReconnectSnapshots.Remove(PlayerKey); }
	void StoreSnapshot(int32 PlayerIndex, const FSWPlayerProgressSnapshot& Snapshot);
	bool ConsumeSnapshot(int32 PlayerIndex, FSWPlayerProgressSnapshot& OutSnapshot);
	bool HasSnapshot(int32 PlayerIndex) const;

	void StoreReconnectSnapshot(const FString& PlayerKey, const FSWPlayerProgressSnapshot& Snapshot);
	bool ConsumeReconnectSnapshot(const FString& PlayerKey, FSWPlayerProgressSnapshot& OutSnapshot);
	bool PeekReconnectSnapshot(const FString& PlayerKey, FSWPlayerProgressSnapshot& OutSnapshot) const;
	void ClearReconnectSnapshots();
	void ClearSnapshotsForHostedReturn();
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override;
	virtual ESWVoyageRestoreStage GetVoyageRestoreStage_Implementation() const override;
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual TArray<FName> GetVoyageAfterParticipants_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void ResumeVoyage_Implementation(const FSWVoyageResetContext& Context) override;
	virtual void CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context) override;

private:
	UPROPERTY(Transient)
	TMap<int32, FSWPlayerProgressSnapshot> PendingSnapshots;

	UPROPERTY(Transient)
	TMap<FString, FSWPlayerProgressSnapshot> ReconnectSnapshots;
	int32 HostedVoyageResetGeneration = -1;
};
