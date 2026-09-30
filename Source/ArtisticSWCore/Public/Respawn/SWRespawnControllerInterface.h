#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Respawn/SWRespawnFlowTypes.h"
#include "SWRespawnControllerInterface.generated.h"
class APawn;
UINTERFACE(MinimalAPI)
class USWRespawnControllerInterface : public UInterface { GENERATED_BODY() };
class ARTISTICSWCORE_API ISWRespawnControllerInterface
{
 GENERATED_BODY()
public:
 virtual bool CaptureLatestLifeProgress(APawn* SourcePawn) = 0;
 virtual void SetDeathFlowState(const FSWDeathFlowState& State) = 0;
 virtual bool HasPendingLifeProgress() const = 0;
 virtual bool ApplyPendingLifeProgress(APawn* NewPawn) = 0;
 virtual bool WasLastLifeProgressApplySuccessful(APawn* NewPawn) const = 0;
 virtual void FreezeLifeProgressForGameOver() = 0;
 virtual void ReleaseFrozenLifeProgress() = 0;
};
