#pragma once

#include "CoreMinimal.h"
#include "Templates/Atomic.h"
#include "Templates/SharedPointer.h"
#include "SWVoyageResetTypes.generated.h"

struct ARTISTICSWCORE_API FSWVoyageAsyncGuard
{
	int32 Generation = 0;
	TAtomic<bool> bCancelled{false};
	TAtomic<bool> bGameplayBlocked{false};
};

UENUM(BlueprintType)
enum class ESWVoyageReason : uint8 { Return, GameOverRetry, FinalDeparture };

enum class ESWVoyageSpawnFinishState : uint8 { Untracked, Deferred, Finishing, Finished, Aborted };

UENUM(BlueprintType)
enum class ESWVoyagePhase : uint8
{
	Idle, Presentation, Quiesce, Unload, Purge, Load, Restore, ClientReady, Save, Release, RecoveryTravel, Failed
};

UENUM(BlueprintType)
enum class ESWVoyageAck : uint8 { Presentation, Unloaded, Loaded, Ready };

UENUM(BlueprintType)
enum class ESWVoyagePolicy : uint8 { Preserve, ResetParticipant, Unsupported };

UENUM(BlueprintType)
enum class ESWVoyageActorLifetime : uint8 { Environment, Anchor, Voyage, SharedService, PlayerLife, LocalPresentation };

UENUM(BlueprintType)
enum class ESWVoyageStepResult : uint8 { Pending, Succeeded, Failed };

UENUM(BlueprintType)
enum class ESWVoyageRestoreStage : uint8 { AuthoredActors, InitialSpawners, SharedState, Players, Readiness };

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWVoyageResetContext
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int64 AttemptId = 0;
	UPROPERTY(BlueprintReadOnly) int32 Generation = 0;
	UPROPERTY(BlueprintReadOnly) ESWVoyageReason Reason = ESWVoyageReason::Return;
	UPROPERTY(BlueprintReadOnly) ESWVoyagePhase Phase = ESWVoyagePhase::Idle;
	UPROPERTY(BlueprintReadOnly) FName GameplayPackage;
	UPROPERTY(BlueprintReadOnly) bool bAuthority = false;
	UPROPERTY(BlueprintReadOnly) bool bBootstrap = false;
	UPROPERTY(BlueprintReadOnly) bool bContinue = false;
	UPROPERTY(BlueprintReadOnly) ESWVoyageRestoreStage RestoreStage = ESWVoyageRestoreStage::AuthoredActors;
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWVoyageReplicatedState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int64 AttemptId = 0;
	UPROPERTY(BlueprintReadOnly) int32 Generation = 0;
	UPROPERTY(BlueprintReadOnly) ESWVoyageReason Reason = ESWVoyageReason::Return;
	UPROPERTY(BlueprintReadOnly) ESWVoyagePhase Phase = ESWVoyagePhase::Idle;
	UPROPERTY(BlueprintReadOnly) FName GameplayPackage;
	UPROPERTY(BlueprintReadOnly) bool bBootstrap = false;
	UPROPERTY(BlueprintReadOnly) bool bContinue = false;
};
