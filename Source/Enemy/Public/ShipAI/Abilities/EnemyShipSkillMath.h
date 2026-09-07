#pragma once

#include "CoreMinimal.h"

/** Deterministic calculations shared by Enemy Ship abilities and automation tests. */
struct ENEMY_API FEnemyShipSkillMath
{
	/** Positive speed at which Source is closing on Target along their center line. */
	static float CalculateApproachSpeed(
		const FVector& SourceLocation,
		const FVector& SourceVelocity,
		const FVector& TargetLocation,
		const FVector& TargetVelocity);

	/** Source-only horizontal closing speed; target velocity is intentionally ignored. */
	static float CalculateSourceApproachSpeed(
		const FVector& SourceLocation,
		const FVector& SourceVelocity,
		const FVector& TargetLocation);

	static float CalculateChargeDamage(
		float ApproachSpeedCmPerSecond,
		float MinimumDamageSpeedMetersPerSecond,
		float MinimumDamage,
		float DamagePerAdditionalMeterPerSecond,
		float MaximumDamage);

	/**
	 * Solves the exact fixed-speed ballistic arc to TargetLocation. Both valid
	 * solutions preserve speed and endpoint; PreferredAngle chooses between them.
	 */
	static bool SuggestBallisticVelocity(
		const FVector& StartLocation,
		const FVector& TargetLocation,
		float LaunchSpeed,
		float GravityMagnitude,
		float PreferredAngleDegrees,
		FVector& OutLaunchVelocity,
		float& OutFlightTime,
		float& OutSolvedAngleDegrees);
};
