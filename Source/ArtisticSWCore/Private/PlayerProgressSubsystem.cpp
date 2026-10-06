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

void UPlayerProgressSubsystem::StoreReconnectSnapshot(const FString& PlayerKey, const FSWPlayerProgressSnapshot& Snapshot)
{
	if (!PlayerKey.IsEmpty()) ReconnectSnapshots.Add(PlayerKey, Snapshot);
}

bool UPlayerProgressSubsystem::ConsumeReconnectSnapshot(const FString& PlayerKey, FSWPlayerProgressSnapshot& OutSnapshot)
{
	if (FSWPlayerProgressSnapshot* Found = ReconnectSnapshots.Find(PlayerKey))
	{
		OutSnapshot = *Found;
		return true;
	}
	return false;
}

bool UPlayerProgressSubsystem::PeekReconnectSnapshot(const FString& PlayerKey, FSWPlayerProgressSnapshot& OutSnapshot) const
{
	if (const FSWPlayerProgressSnapshot* Found = ReconnectSnapshots.Find(PlayerKey))
	{
		OutSnapshot = *Found;
		return true;
	}
	return false;
}

void UPlayerProgressSubsystem::ClearReconnectSnapshots()
{
	ReconnectSnapshots.Reset();
}

void UPlayerProgressSubsystem::ClearSnapshotsForHostedReturn()
{
	PendingSnapshots.Reset();
	ClearReconnectSnapshots();
}
