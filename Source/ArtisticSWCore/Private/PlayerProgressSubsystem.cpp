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
