#pragma once

#include "CoreMinimal.h"
#include "ProjectileLaunchTypes.generated.h"

/** The meaning of authored speed is a weapon rule, never inferred from standing on a ship. */
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
	FVector ShooterVelocity = FVector::ZeroVector;
	FName CarrierName;
	double CommitServerTime = 0.0;
	uint64 CommitFrame = 0;
	bool bHasMotionSample = false;
};
