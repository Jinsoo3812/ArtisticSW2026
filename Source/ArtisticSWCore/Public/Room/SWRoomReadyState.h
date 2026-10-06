#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Respawn/SWRespawnFlowTypes.h"
#include "Room/SWVoyageResetTypes.h"
#include "SWRoomReadyState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FSWOnVoyageStateChanged, const FSWVoyageReplicatedState&);

/** Replicated barrier for the currently restored hosted-room world. */
UCLASS()
class ARTISTICSWCORE_API ASWRoomReadyState : public AInfo
{
	GENERATED_BODY()
public:
	UPROPERTY(Replicated) ESWSessionLifePhase SessionLifePhase = ESWSessionLifePhase::Playing;
	ASWRoomReadyState();
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Room") int32 RestoreGeneration = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Room") bool bWorldReady = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Room") FGuid RoomRunId;
	UPROPERTY(ReplicatedUsing=OnRep_VoyageState, BlueprintReadOnly, Category="Room") FSWVoyageReplicatedState VoyageState;
	FSWOnVoyageStateChanged OnVoyageStateChanged;
	void PublishVoyageState(const FSWVoyageReplicatedState& State);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UFUNCTION() void OnRep_VoyageState();
};
