#pragma once

#include "CoreMinimal.h"
#include "Skills/PlayerSkillGameplayAbility.h"
#include "GA_GravityVortexThrow.generated.h"

class AGravityVortexProjectile;
class ABasePlayer;
class AVortexAimLine;
class USkeletalMeshComponent;

/** Hold the skill key to aim, press left mouse to throw, or right mouse/release the skill key to cancel. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UGA_GravityVortexThrow : public UPlayerSkillGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_GravityVortexThrow();

	/** Resolves an authored socket first, then a fallback bone, across all player skeletal meshes. */
	static bool ResolveSpawnSocket(
		const ABasePlayer* Player,
		FName RequestedSocketName,
		FName FallbackBoneName,
		FVector& OutWorldLocation,
		const USkeletalMeshComponent*& OutMesh,
		FName& OutResolvedName);

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
	FName SpawnSocketName = TEXT("HandGrip_R");

	/** Used only when SpawnSocketName is missing from every skeletal mesh on the player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Throw")
	FName FallbackSpawnBoneName = TEXT("hand_r");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug")
	bool bDrawAimTrajectory = false;

	/**
	 * Sends the predicted world-space trajectory to Blueprint while aiming.
	 * Implement K2_OnAimTrajectoryUpdated in the GA Blueprint to drive a Niagara
	 * ribbon or spline-mesh preview. This is independent from debug drawing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Visual")
	bool bUpdateAimTrajectoryVisual = true;

	/**
	 * Optional zero-graph aim-line actor. Reparent BP_VortexAimLine to
	 * AVortexAimLine, assign it here, then set its mesh and material.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Visual")
	TSubclassOf<AVortexAimLine> AimLineClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gravity Vortex|Debug", meta = (ClampMin = "0.01", Units = "s"))
	float TrajectoryRefreshInterval = 0.05f;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Gravity Vortex|Visual", meta = (DisplayName = "On Aim Trajectory Updated"))
	void K2_OnAimTrajectoryUpdated(const TArray<FVector>& WorldPoints);

	UFUNCTION(BlueprintImplementableEvent, Category = "Gravity Vortex|Visual", meta = (DisplayName = "On Aim Trajectory Cleared"))
	void K2_OnAimTrajectoryCleared();

	UFUNCTION()
	void OnLeftClickPressed(FGameplayEventData Payload);

	UFUNCTION()
	void OnRightClickPressed(FGameplayEventData Payload);

	UFUNCTION()
	void OnActivationInputReleased(float TimeHeld);

	UFUNCTION()
	void DrawAimTrajectory();

private:
	bool GetLaunchData(FVector& OutSpawnLocation, FVector& OutLaunchVelocity) const;
	void SpawnProjectileOnServer();

	bool bThrowRequested = false;
	mutable bool bLoggedLaunchResolution = false;
	bool bLoggedAimLineResolution = false;
	FTimerHandle TrajectoryTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AVortexAimLine> AimLineActor;
};
