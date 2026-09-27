#pragma once

#include "CoreMinimal.h"
#include "RollTypes.generated.h"

/**
 * Animation-system-independent description of a requested roll.
 *
 * The MVP montage executor consumes this to orient the character. A future
 * Motion Matching executor can consume the same snapshot for trajectory and
 * pose-selection inputs without depending on Gameplay Ability implementation.
 */
USTRUCT(BlueprintType)
struct CLASSFEATURE_API FRollIntent
{
	GENERATED_BODY()

	/** Normalized horizontal direction in world space. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	FVector WorldDirection = FVector::ForwardVector;

	/** WorldDirection expressed in the avatar's local space at activation. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	FVector LocalDirection = FVector::ForwardVector;

	/** Last movement-input magnitude before horizontal normalization. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	float InputMagnitude = 0.0f;

	/** Horizontal velocity captured when the ability activated. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	FVector InitialVelocity = FVector::ZeroVector;

	/** Facing requested by this roll, independent of the animation executor. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	FRotator RequestedFacingRotation = FRotator::ZeroRotator;

	/** False when the ability fell back to the avatar's forward direction. */
	UPROPERTY(BlueprintReadOnly, Category = "Roll")
	bool bHasMovementInput = false;
};
