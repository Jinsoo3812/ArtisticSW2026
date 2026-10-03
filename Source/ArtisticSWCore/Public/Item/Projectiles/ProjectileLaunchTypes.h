#pragma once

#include "CoreMinimal.h"
#include "ProjectileLaunchTypes.generated.h"

/** Fixed by the weapon's shot preparation. Player bows always use PhysicalInheritance. */
UENUM(BlueprintType)
enum class EProjectileVelocityPolicy : uint8
{
	WorldAim,
	PhysicalInheritance
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FProjectileLaunchProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
	EProjectileVelocityPolicy VelocityPolicy = EProjectileVelocityPolicy::WorldAim;
};

/** Numeric input, in world space. No target tracking or platform references enter the solver. */
struct ARTISTICSWCORE_API FProjectileShotInput
{
	FGuid ShotId;
	FTransform MuzzleTransform = FTransform::Identity;
	FVector AimPoint = FVector::ZeroVector;
	FVector AimDirection = FVector::ForwardVector;
	double Speed = 0.0;
	double GravityZ = 0.0;
	double AimServerTime = 0.0;
	FProjectileLaunchProfile Profile;
};

/** Produced once at commit, consumed unchanged by spawning, movement and diagnostics. */
struct ARTISTICSWCORE_API FProjectileShotSnapshot
{
	FProjectileShotInput Input;
	FTransform SpawnTransform = FTransform::Identity;
	FVector WorldVelocity = FVector::ZeroVector;
	/** Added at commit only. Player bows inherit the ship's socket point velocity, excluding locomotion. */
	FVector InheritedVelocity = FVector::ZeroVector;
	/** Enemy diagnostic sample; never implicitly added by spawning or flight. */
	FVector ShooterVelocity = FVector::ZeroVector;
	FName CarrierName;
	double CommitServerTime = 0.0;
	uint64 CommitFrame = 0;
	bool bHasMotionSample = false;
};
