#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
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

private:
	bool BuildShotSolution(
		const ACannon* Cannon,
		const AShip* Target,
		const AEnemyShip* Ship,
		FVector& OutDirection,
		float& OutProjectileSpeed) const;
	bool IsValidPlayerTarget(const AShip* Candidate) const;
};
