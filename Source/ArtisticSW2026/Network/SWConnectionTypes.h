#pragma once

#include "CoreMinimal.h"
#include "SWConnectionTypes.generated.h"

UENUM(BlueprintType)
enum class ESWConnectionState : uint8
{
	Idle, Connecting, LoadingMap, Synchronizing, Playing, Failed
};

UENUM(BlueprintType)
enum class ESWConnectionFailureReason : uint8
{
	None, InvalidAddress, NoLocalPlayerController, ServerFull, ConnectionTimeout,
	ConnectionLost, VersionMismatch, NetworkFailure, TravelFailure
};

USTRUCT(BlueprintType)
struct FSWConnectionFailure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	ESWConnectionFailureReason Reason = ESWConnectionFailureReason::None;

	UPROPERTY(BlueprintReadOnly)
	FString EngineFailureType;

	UPROPERTY(BlueprintReadOnly)
	FString EngineMessage;

	UPROPERTY(BlueprintReadOnly)
	int32 AttemptId = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSWConnectionStateChanged, ESWConnectionState, PreviousState, ESWConnectionState, NewState, int32, AttemptId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSWConnectionFailed, FSWConnectionFailure, Failure);
