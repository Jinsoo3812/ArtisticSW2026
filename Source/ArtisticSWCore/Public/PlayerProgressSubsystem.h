#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlayerRespawnTypes.h"
#include "PlayerProgressSubsystem.generated.h"

/** Transient bridge that survives an OpenLevel and preserves per-player run progress. */
UCLASS()
class ARTISTICSWCORE_API UPlayerProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void StoreSnapshot(int32 PlayerIndex, const FSWPlayerProgressSnapshot& Snapshot);
	bool ConsumeSnapshot(int32 PlayerIndex, FSWPlayerProgressSnapshot& OutSnapshot);
	bool HasSnapshot(int32 PlayerIndex) const;

	bool FindReconnectRecord(const FGuid& ReconnectToken, double CurrentTimeSeconds, FSWReconnectRecord& OutRecord);
	bool IsPlayerIndexReserved(int32 PlayerIndex, double CurrentTimeSeconds);
	bool ActivateReconnectRecord(const FGuid& ReconnectToken, int32 PlayerIndex, double CurrentTimeSeconds);
	void CancelReconnectActivation(const FGuid& ReconnectToken, bool bRemoveNewRecord);
	bool StoreReconnectSnapshot(const FGuid& ReconnectToken, int32 PlayerIndex,
		const FSWPlayerProgressSnapshot& Snapshot, double CurrentTimeSeconds, double ReservationDurationSeconds);
	void MarkReconnectDisconnected(const FGuid& ReconnectToken, int32 PlayerIndex,
		double CurrentTimeSeconds, double ReservationDurationSeconds);
	bool ConsumeReconnectSnapshot(const FGuid& ReconnectToken, FSWPlayerProgressSnapshot& OutSnapshot);
	bool PeekReconnectSnapshot(const FGuid& ReconnectToken, FSWPlayerProgressSnapshot& OutSnapshot) const;
	void PruneExpiredReconnectRecords(double CurrentTimeSeconds);

private:
	UPROPERTY(Transient)
	TMap<int32, FSWPlayerProgressSnapshot> PendingSnapshots;

	UPROPERTY(Transient)
	TMap<FGuid, FSWReconnectRecord> ReconnectRecords;
};
