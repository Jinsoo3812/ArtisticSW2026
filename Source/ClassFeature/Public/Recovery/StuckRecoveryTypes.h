#pragma once

#include "CoreMinimal.h"
#include "StuckRecoveryTypes.generated.h"

UENUM(BlueprintType)
enum class EStuckRecoveryState : uint8
{
	Idle, Monitoring, SuspectedStuck, ConfirmedStuck, Recovering, Cooldown
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FStuckDetectionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.02", Units="s"))
	float DetectionInterval = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float MovementModeMonitorDuration = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float PostAbilityMonitorDuration = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float FallbackInactivityDuration = 5.f;
	/** Idle players need only occasional collision probes until suspicious evidence appears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float FallbackProbeInterval = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float BlockedInputDuration = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float StuckConfirmDuration = 0.75f;
	/** Speed threshold makes the displacement tolerance independent of sampling rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm/s"))
	float StationarySpeed = 30.f;
	/** Shrinks detection queries to exclude normal floor contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm"))
	float PenetrationInset = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", Units="cm"))
	float ObstructionProbeDistance = 20.f;
};

USTRUCT(BlueprintType)
struct CLASSFEATURE_API FStuckEscapeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float HoldDuration = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm"))
	float ForwardDistance = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm"))
	float UpDistance = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", ClampMax="8"))
	int32 MaxEscapeAttempts = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="s"))
	float Cooldown = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", Units="cm"))
	float MaxLandingDrop = 250.f;
	/** Bounds the final blocked segment of a reverse sweep when escaping penetration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm"))
	float MaxPenetrationExitDistance = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowPenetrationRecovery = true;
};
