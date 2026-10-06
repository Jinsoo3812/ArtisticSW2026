#include "Room/SWRoomReadyState.h"
#include "Net/UnrealNetwork.h"

ASWRoomReadyState::ASWRoomReadyState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = false;
}

void ASWRoomReadyState::PublishVoyageState(const FSWVoyageReplicatedState& State)
{
	check(HasAuthority());
	if (VoyageState.AttemptId == State.AttemptId && VoyageState.Generation == State.Generation
		&& VoyageState.Reason == State.Reason && VoyageState.Phase == State.Phase
		&& VoyageState.GameplayPackage == State.GameplayPackage
		&& VoyageState.bBootstrap == State.bBootstrap && VoyageState.bContinue == State.bContinue) return;
	VoyageState = State;
	ForceNetUpdate();
	OnVoyageStateChanged.Broadcast(VoyageState);
}

void ASWRoomReadyState::OnRep_VoyageState()
{
	OnVoyageStateChanged.Broadcast(VoyageState);
}

void ASWRoomReadyState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASWRoomReadyState, RestoreGeneration);
	DOREPLIFETIME(ASWRoomReadyState, bWorldReady);
	DOREPLIFETIME(ASWRoomReadyState, RoomRunId);
	DOREPLIFETIME(ASWRoomReadyState, SessionLifePhase);
	DOREPLIFETIME(ASWRoomReadyState, VoyageState);
}
