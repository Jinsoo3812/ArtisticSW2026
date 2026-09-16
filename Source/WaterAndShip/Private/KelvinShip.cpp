#include "KelvinShip.h"

#include "SWShipWakeEmitterComponent.h"
#include "PlayerRespawnPointComponent.h"

AKelvinShip::AKelvinShip()
{
	ShipWakeEmitter = CreateDefaultSubobject<USWShipWakeEmitterComponent>(TEXT("ShipWakeEmitter"));
	Player0RespawnPoint = CreateDefaultSubobject<UPlayerRespawnPointComponent>(TEXT("Player0RespawnPoint"));
	Player0RespawnPoint->SetupAttachment(GetRootComponent());
	Player0RespawnPoint->SetRelativeLocation(FVector(0.0, -120.0, 420.0));
	Player0RespawnPoint->PlayerSlot = ESWPlayerSlot::Player0;
	Player1RespawnPoint = CreateDefaultSubobject<UPlayerRespawnPointComponent>(TEXT("Player1RespawnPoint"));
	Player1RespawnPoint->SetupAttachment(GetRootComponent());
	Player1RespawnPoint->SetRelativeLocation(FVector(0.0, 120.0, 420.0));
	Player1RespawnPoint->PlayerSlot = ESWPlayerSlot::Player1;
}

