#pragma once

#include "CoreMinimal.h"
#include "EnemyShipRuntimeState.generated.h"

UENUM(BlueprintType)
enum class EEnemyShipRuntimePhase : uint8
{
	Restoring,
	Dormant,
	Active,
	Terminal
};

enum class EEnemyShipRuntimeBlock : uint8
{
	Distance = 1,
	Story = 2,
	Restore = 4,
	Terminal = 8,
	CrewDefeated = 16
};

/** One replicated presentation snapshot; server-local delegates carry the same committed state. */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipRuntimeState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EEnemyShipRuntimePhase Phase = EEnemyShipRuntimePhase::Restoring;
	UPROPERTY(BlueprintReadOnly) uint8 BlockingReasons = 0;
	UPROPERTY() uint32 Revision = 0;
	bool IsActive() const { return Phase == EEnemyShipRuntimePhase::Active; }
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEnemyShipRuntimeStateChanged,
	const FEnemyShipRuntimeState&, const FEnemyShipRuntimeState&);
