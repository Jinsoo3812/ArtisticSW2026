#pragma once
#include "CoreMinimal.h"
#include "SWRespawnFlowTypes.generated.h"
class APlayerState;
enum class ESWLifeRestoreStepState : uint8 { Pending, Failed, Succeeded };
struct ARTISTICSWCORE_API FSWLifeRestoreStatus
{
 bool bContractValid = true;
 ESWLifeRestoreStepState Apply = ESWLifeRestoreStepState::Pending;
 ESWLifeRestoreStepState LifeInitialization = ESWLifeRestoreStepState::Pending;
 ESWLifeRestoreStepState InitialPossession = ESWLifeRestoreStepState::Pending;
 FString Error;
};
UENUM()
enum class ESWSessionLifePhase : uint8 { Playing, ShipSinking, GameOver, ReturningAfterGameOver };
UENUM()
enum class ESWPersonalLifePhase : uint8 { Alive, WaitingForRespawn };
USTRUCT()
struct ARTISTICSWCORE_API FSWDeathFlowState
{
 GENERATED_BODY()
 UPROPERTY() ESWPersonalLifePhase Phase = ESWPersonalLifePhase::Alive;
 UPROPERTY() double RespawnEndServerTime = 0;
 UPROPERTY() int32 WaitingGeneration = 0;
 UPROPERTY() int32 RestoreGeneration = 0;
 UPROPERTY() int32 ObservationGeneration = 0;
 UPROPERTY() TObjectPtr<APlayerState> SpectatedPlayerState = nullptr;
 UPROPERTY() bool bHostMayRetry = false;
};
USTRUCT()
struct ARTISTICSWCORE_API FSWObservedCameraFrame
{
 GENERATED_BODY()
 UPROPERTY() FVector Location = FVector::ZeroVector;
 UPROPERTY() FRotator Rotation = FRotator::ZeroRotator;
 UPROPERTY() float FOV = 90;
 UPROPERTY() double SampleServerTime = 0;
 UPROPERTY() int32 RestoreGeneration = 0;
 UPROPERTY() int32 ObservationGeneration = 0;
 UPROPERTY() uint32 Sequence = 0;
 UPROPERTY() TObjectPtr<APlayerState> SourcePlayerState = nullptr;
};
