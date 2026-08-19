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

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Cannon Volley", meta = (ClampMin = "1"))
	int32 MaxCannonsPerVolley = 2;

	/** Chooses the exact fixed-speed ballistic solution closest to this angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Cannon Volley", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = "deg"))
	float PreferredLaunchAngleDegrees = 20.0f;

private:
	bool BuildShotDirection(const ACannon* Cannon, const AShip* Target, FVector& OutDirection) const;
	bool IsValidPlayerTarget(const AShip* Candidate) const;
};
