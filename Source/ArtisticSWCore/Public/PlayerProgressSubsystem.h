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

private:
	UPROPERTY(Transient)
	TMap<int32, FSWPlayerProgressSnapshot> PendingSnapshots;
};
