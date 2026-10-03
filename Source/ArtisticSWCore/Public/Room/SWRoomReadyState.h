#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Respawn/SWRespawnFlowTypes.h"
#include "SWRoomReadyState.generated.h"

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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
