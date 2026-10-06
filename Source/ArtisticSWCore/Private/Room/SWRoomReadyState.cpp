#include "Room/SWRoomReadyState.h"
#include "Net/UnrealNetwork.h"

ASWRoomReadyState::ASWRoomReadyState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = false;
}

void ASWRoomReadyState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASWRoomReadyState, RestoreGeneration);
	DOREPLIFETIME(ASWRoomReadyState, bWorldReady);
	DOREPLIFETIME(ASWRoomReadyState, RoomRunId);
	DOREPLIFETIME(ASWRoomReadyState, SessionLifePhase);
}
