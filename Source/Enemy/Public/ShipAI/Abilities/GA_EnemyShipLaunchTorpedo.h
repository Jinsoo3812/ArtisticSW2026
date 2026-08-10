#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "GA_EnemyShipLaunchTorpedo.generated.h"

class ACannon;
class AEnemyShip;
class AEnemyShipTorpedo;
class AShip;

/** Selects the closest mounted cannon and launches one fixed-speed ballistic torpedo. */
UCLASS(Blueprintable)
class ENEMY_API UGA_EnemyShipLaunchTorpedo : public UEnemyShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EnemyShipLaunchTorpedo();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	static ACannon* SelectClosestCannon(const AEnemyShip* Ship, const FVector& TargetLocation);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo")
	TSubclassOf<AEnemyShipTorpedo> TorpedoClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (ClampMin = "0.0"))
	float TorpedoDamageMultiplier = 1.5f;

	/** Chooses between the two exact fixed-speed ballistic solutions. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = "deg"))
	float PreferredLaunchAngleDegrees = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (Units = "m"))
	float TargetForwardOffsetMeters = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (ClampMin = "0.1", Units = "s"))
	float MaximumFlightSeconds = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (ClampMin = "1.0", Units = "cm"))
	float ImpactTolerance = 100.0f;

private:
	bool IsValidPlayerTarget(const AShip* Candidate) const;
};
