#pragma once

#include "CoreMinimal.h"
#include "Recovery/StuckRecoveryTypes.h"

class ACharacter;
class UPrimitiveComponent;

/** Read-only collision policy, independent of input, GAS and the state machine. */
namespace StuckRecoveryCollision
{
	bool IsPenetrating(const ACharacter& Character, float Inset);
	bool IsEnclosed(const ACharacter& Character, const FStuckDetectionSettings& Settings);
	bool FindDestination(const ACharacter& Character, const FStuckEscapeSettings& Settings,
		FVector& OutDestination);
}
