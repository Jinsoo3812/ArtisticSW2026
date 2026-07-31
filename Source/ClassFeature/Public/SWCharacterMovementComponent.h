#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SWCharacterMovementComponent.generated.h"

enum class ESwimDepthMode : uint8;

/**
 * Custom character movement component that handles custom movement modes,
 * specifically custom swimming movement to support smooth client prediction.
 */
UCLASS()
class CLASSFEATURE_API USWCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** Routes the local command through the movement component so CMC can save and replay it. */
	void SetSwimmingVerticalInput(float InVerticalInput);

	/** Returns the command that will be captured in the next CMC saved move. */
	float GetSwimmingVerticalInput() const;

	/** Returns the swimming sub-state that will be restored during CMC replay. */
	ESwimDepthMode GetSwimmingDepthMode() const;

	/** Restores input and sub-state before replaying a CMC saved move. */
	void RestoreSavedSwimmingState(float InVerticalInput, ESwimDepthMode InDepthMode);

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};
