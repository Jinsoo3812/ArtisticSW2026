#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_GravityVortexThrow.generated.h"

class AGravityVortexProjectile;

/** Hold the bound key to aim, then press left mouse to throw. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UGA_GravityVortexThrow : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_GravityVortexThrow();

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw")
	TSubclassOf<AGravityVortexProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ThrowSpeed = 2200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw")
	float UpwardAimBias = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw")
	float SpawnForwardOffset = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw")
	float SpawnVerticalOffset = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw")
	FName SpawnSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug")
	bool bDrawAimTrajectory = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug", meta = (ClampMin = "0.01", Units = "s"))
	float TrajectoryRefreshInterval = 0.05f;

protected:
	UFUNCTION()
	void OnLeftClickPressed(FGameplayEventData Payload);

	UFUNCTION()
	void OnActivationInputReleased(float TimeHeld);

	UFUNCTION()
	void DrawAimTrajectory();

private:
	bool GetLaunchData(FVector& OutSpawnLocation, FVector& OutLaunchVelocity) const;
	void SpawnProjectileOnServer();

	bool bThrowRequested = false;
	FTimerHandle TrajectoryTimerHandle;
};
