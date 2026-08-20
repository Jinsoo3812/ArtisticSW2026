#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DeckWaypointMovementInterface.generated.h"

class AEnemyShip;

/** Minimal native contract for characters that move between live ship-deck waypoints. */
UINTERFACE(MinimalAPI)
class UDeckWaypointMovementInterface : public UInterface
{
	GENERATED_BODY()
};

class ENEMY_API IDeckWaypointMovementInterface
{
	GENERATED_BODY()

public:
	virtual AEnemyShip* GetDeckHostShip() const = 0;
	virtual int32 GetCurrentDeckPointId() const = 0;
	virtual int32 GetGoalDeckPointId() const = 0;
	virtual void OnDeckPointReached() = 0;
	virtual void OnDeckMoveFailed() = 0;
	virtual bool CanMoveOnDeck() const = 0;
};
