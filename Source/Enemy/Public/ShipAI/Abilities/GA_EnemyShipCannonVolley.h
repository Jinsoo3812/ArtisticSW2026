#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "ShipAI/EnemyShipSkillModuleData.h"
#include "GA_EnemyShipCannonVolley.generated.h"

class ACannon;
class AEnemyShip;
class AShip;

/** Fires one normal ballistic volley without owning the cannons' reload cooldowns. */
UCLASS(Blueprintable)
class ENEMY_API UGA_EnemyShipCannonVolley : public UEnemyShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EnemyShipCannonVolley();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	static bool CalculateLaunchVelocity(
		const FVector& Start,
		const FVector& CurrentTargetPoint,
		const FVector& TargetVelocity,
		float GravityZ,
		const struct FEnemyShipCannonAimProfile& AimProfile,
		FVector& OutLaunchVelocity);

	static bool CalculateRangeAtElevation(
		float ProjectileSpeed, float GravityMagnitude, float DeltaZ, float ElevationRadians,
		float& OutRangeCm, float& OutFlightTime);
	static bool CalculateLowArc(
		float ProjectileSpeed, float GravityMagnitude, float HorizontalRangeCm, float DeltaZ,
		float& OutElevationRadians, float& OutFlightTime);
	static FVector SampleImpactEllipseOffset(
		FRandomStream& RandomStream, const FVector& TargetForward2D, const FVector& TargetRight2D,
		const FVector& DirectionFromTargetToAttacker2D,
		const FEnemyShipCannonVolleySettings& Settings, bool& bOutFacingHalf);
	static FVector ClampImpactPointToRange(
		const FVector& ShotStart, const FVector& RequestedImpactPoint, float MaximumRangeCm,
		bool& bOutWasClamped);

private:
	struct FCannonTargetingDebugData
	{
		FVector Start = FVector::ZeroVector;
		FVector CurrentTargetPoint = FVector::ZeroVector;
		FVector TargetVelocity = FVector::ZeroVector;
		FVector TargetForward = FVector::ForwardVector;
		FVector TargetRight = FVector::RightVector;
		FVector EllipseCenter = FVector::ZeroVector;
		FVector RequestedImpactPoint = FVector::ZeroVector;
		FVector ClampedImpactPoint = FVector::ZeroVector;
		FEnemyShipCannonVolleySettings Settings;
		float CurrentDistance = 0.0f;
		float MaximumRange = 0.0f;
		float EstimatedFlightTime = 0.0f;
		float CalculatedElevation = 0.0f;
		float FinalElevation = 0.0f;
		bool bInsideRange = false;
		bool bFacingHalf = false;
		bool bRangeClamped = false;
	};

	static FEnemyShipCannonVolleySettings ResolveCannonVolleySettings(const AEnemyShip& Ship);
	static void DrawCannonTargetingDebug(
		const ACannon& Cannon, const AShip& Target, const AEnemyShip& Ship,
		const FVector& Direction, float ProjectileSpeed, const FCannonTargetingDebugData& DebugData);
	bool BuildShotSolution(
		const ACannon* Cannon,
		const AShip* Target,
		const AEnemyShip* Ship,
		FVector& OutDirection,
		float& OutProjectileSpeed,
		FCannonTargetingDebugData* OutDebugData) const;
	bool IsValidPlayerTarget(const AShip* Candidate) const;
};
