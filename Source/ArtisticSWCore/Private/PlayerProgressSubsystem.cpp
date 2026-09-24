#include "PlayerProgressSubsystem.h"

void UPlayerProgressSubsystem::StoreSnapshot(int32 PlayerIndex, const FSWPlayerProgressSnapshot& Snapshot)
{
	PendingSnapshots.Add(PlayerIndex, Snapshot);
}

bool UPlayerProgressSubsystem::ConsumeSnapshot(int32 PlayerIndex, FSWPlayerProgressSnapshot& OutSnapshot)
{
	if (FSWPlayerProgressSnapshot* Found = PendingSnapshots.Find(PlayerIndex))
	{
		OutSnapshot = MoveTemp(*Found);
		PendingSnapshots.Remove(PlayerIndex);
		return true;
	}
	return false;
}

bool UPlayerProgressSubsystem::HasSnapshot(int32 PlayerIndex) const
{
	return PendingSnapshots.Contains(PlayerIndex);
}

bool UPlayerProgressSubsystem::FindReconnectRecord(
	const FGuid& ReconnectToken,
	double CurrentTimeSeconds,
	FSWReconnectRecord& OutRecord)
{
	PruneExpiredReconnectRecords(CurrentTimeSeconds);
	if (const FSWReconnectRecord* Record = ReconnectRecords.Find(ReconnectToken))
	{
		OutRecord = *Record;
		return true;
	}
	return false;
}

bool UPlayerProgressSubsystem::IsPlayerIndexReserved(int32 PlayerIndex, double CurrentTimeSeconds)
{
	PruneExpiredReconnectRecords(CurrentTimeSeconds);
	for (const TPair<FGuid, FSWReconnectRecord>& Pair : ReconnectRecords)
	{
		if (Pair.Value.PlayerIndex == PlayerIndex)
		{
			return true;
		}
	}
	return false;
}

bool UPlayerProgressSubsystem::ActivateReconnectRecord(
	const FGuid& ReconnectToken,
	int32 PlayerIndex,
	double CurrentTimeSeconds)
{
	PruneExpiredReconnectRecords(CurrentTimeSeconds);
	if (FSWReconnectRecord* Existing = ReconnectRecords.Find(ReconnectToken))
	{
		if (Existing->bConnectionActive || Existing->PlayerIndex != PlayerIndex)
		{
			return false;
		}
		Existing->bConnectionActive = true;
		return true;
	}

	FSWReconnectRecord& NewRecord = ReconnectRecords.Add(ReconnectToken);
	NewRecord.ReconnectToken = ReconnectToken;
	NewRecord.PlayerIndex = PlayerIndex;
	NewRecord.bConnectionActive = true;
	return true;
}

void UPlayerProgressSubsystem::CancelReconnectActivation(
	const FGuid& ReconnectToken,
	bool bRemoveNewRecord)
{
	if (bRemoveNewRecord)
	{
		ReconnectRecords.Remove(ReconnectToken);
		return;
	}

	if (FSWReconnectRecord* Record = ReconnectRecords.Find(ReconnectToken))
	{
		Record->bConnectionActive = false;
	}
}

bool UPlayerProgressSubsystem::StoreReconnectSnapshot(
	const FGuid& ReconnectToken,
	int32 PlayerIndex,
	const FSWPlayerProgressSnapshot& Snapshot,
	double CurrentTimeSeconds,
	double ReservationDurationSeconds)
{
	FSWReconnectRecord* Record = ReconnectRecords.Find(ReconnectToken);
	if (!Record || Record->PlayerIndex != PlayerIndex)
	{
		return false;
	}

	Record->Snapshot = Snapshot;
	Record->bHasSnapshot = true;
	Record->bConnectionActive = false;
	Record->DisconnectTimeSeconds = CurrentTimeSeconds;
	Record->ExpirationTimeSeconds = CurrentTimeSeconds + FMath::Max(ReservationDurationSeconds, 0.0);
	return true;
}

void UPlayerProgressSubsystem::MarkReconnectDisconnected(
	const FGuid& ReconnectToken,
	int32 PlayerIndex,
	double CurrentTimeSeconds,
	double ReservationDurationSeconds)
{
	if (FSWReconnectRecord* Record = ReconnectRecords.Find(ReconnectToken))
	{
		if (Record->PlayerIndex == PlayerIndex)
		{
			Record->bConnectionActive = false;
			Record->DisconnectTimeSeconds = CurrentTimeSeconds;
			Record->ExpirationTimeSeconds = CurrentTimeSeconds + FMath::Max(ReservationDurationSeconds, 0.0);
		}
	}
}

bool UPlayerProgressSubsystem::ConsumeReconnectSnapshot(
	const FGuid& ReconnectToken,
	FSWPlayerProgressSnapshot& OutSnapshot)
{
	FSWReconnectRecord* Record = ReconnectRecords.Find(ReconnectToken);
	if (!Record || !Record->bConnectionActive || !Record->bHasSnapshot)
	{
		return false;
	}

	OutSnapshot = MoveTemp(Record->Snapshot);
	Record->Snapshot = FSWPlayerProgressSnapshot();
	Record->bHasSnapshot = false;
	return true;
}

bool UPlayerProgressSubsystem::PeekReconnectSnapshot(
	const FGuid& ReconnectToken,
	FSWPlayerProgressSnapshot& OutSnapshot) const
{
	const FSWReconnectRecord* Record = ReconnectRecords.Find(ReconnectToken);
	if (!Record || !Record->bConnectionActive || !Record->bHasSnapshot)
	{
		return false;
	}

	OutSnapshot = Record->Snapshot;
	return true;
}

void UPlayerProgressSubsystem::PruneExpiredReconnectRecords(double CurrentTimeSeconds)
{
	for (auto It = ReconnectRecords.CreateIterator(); It; ++It)
	{
		const FSWReconnectRecord& Record = It.Value();
		if (!Record.bConnectionActive && Record.ExpirationTimeSeconds <= CurrentTimeSeconds)
		{
			It.RemoveCurrent();
		}
	}
}

void UPlayerProgressSubsystem::ClearSnapshotsForHostedReturn()
{
	PendingSnapshots.Reset();
	for (TPair<FGuid, FSWReconnectRecord>& Pair : ReconnectRecords)
	{
		Pair.Value.Snapshot = FSWPlayerProgressSnapshot();
		Pair.Value.bHasSnapshot = false;
	}
}
