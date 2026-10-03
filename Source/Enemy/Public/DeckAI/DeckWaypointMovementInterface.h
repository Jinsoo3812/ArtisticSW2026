#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DeckWaypointMovementInterface.generated.h"

class AEnemyShip;

/** Native movement lifecycle shared by deck enemies and bosses. */
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
	virtual void OnDeckMoveReached() = 0;
	virtual void OnDeckMoveFailed() = 0;
	virtual bool CanMoveOnDeck() const = 0;
};
