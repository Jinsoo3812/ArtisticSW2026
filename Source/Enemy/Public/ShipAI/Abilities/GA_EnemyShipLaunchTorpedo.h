#pragma once

#include "CoreMinimal.h"
#include "ShipAI/Abilities/EnemyShipGameplayAbility.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "GA_EnemyShipLaunchTorpedo.generated.h"

class ACannon;
class AEnemyShip;
class AEnemyShipTorpedo;
class AShip;

/** Launches a timed straight-line torpedo volley from the currently closest mounted cannon. */
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
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	static ACannon* SelectClosestCannon(const AEnemyShip* Ship, const FVector& TargetLocation);
	static FVector CalculateLineTargetPoint(
		const FVector& EnemyShipLocation,
		const FVector& PlayerShipLocation,
		float LineAlpha);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo")
	TSubclassOf<AEnemyShipTorpedo> TorpedoClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (ClampMin = "0.0"))
	float TorpedoDamageMultiplier = 1.5f;

	/** Internal division of the Enemy-to-Player segment: 0 = Enemy, 1 = Player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float TargetLineAlpha = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Volley", meta = (ClampMin = "1"))
	int32 TorpedoCount = 3;

	/** Time from the first launch to the final launch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Volley", meta = (ClampMin = "0.0", Units = "s"))
	float VolleyDurationSeconds = 3.0f;

	/** Total lifetime measured from launch; the torpedo may float until this expires. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo", meta = (DisplayName = "Torpedo Lifetime Seconds", ClampMin = "0.1", Units = "s"))
	float MaximumFlightSeconds = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Aiming", meta = (ClampMin = "0.01", Units = "s"))
	float AimUpdateIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Aiming", meta = (ClampMin = "0.0"))
	float ShipTurnResponsiveness = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Torpedo|Aiming", meta = (ClampMin = "0.0"))
	float ShipTurnMultiplier = 2.0f;

private:
	void LaunchNextTorpedo();
	void UpdateVolleyAiming();
	bool FireSingleTorpedo();
	bool IsValidPlayerTarget(const AShip* Candidate) const;

	TWeakObjectPtr<AEnemyShip> ActiveShip;
	TWeakObjectPtr<AShip> ActiveTarget;
	TWeakObjectPtr<ACannon> ActiveCannon;
	FEnemyShipNavigationOverrideHandle NavigationOverrideHandle;
	FTimerHandle VolleyTimerHandle;
	FTimerHandle AimTimerHandle;
	int32 LaunchedTorpedoCount = 0;
};
