#pragma once

#include "CoreMinimal.h"
#include "BossTargetSnapshotTypes.generated.h"

/** Geometry in the DeckWalkArea reference frame; never holds a player actor alive. */
USTRUCT()
struct ENEMY_API FBossTargetSnapshot
{
	GENERATED_BODY()
	UPROPERTY() FVector LocalFloor = FVector::ZeroVector;
	UPROPERTY() FVector LocalForward = FVector::ForwardVector;
	UPROPERTY() FName SurfaceId;
	UPROPERTY() bool bValid = false;
};
