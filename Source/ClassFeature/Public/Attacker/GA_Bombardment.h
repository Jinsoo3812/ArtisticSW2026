#pragma once

#include "CoreMinimal.h"
#include "Skills/PlayerSkillGameplayAbility.h"
#include "GA_Bombardment.generated.h"

class ABombardment;
class AShip;

/** Persistent server ability used while the player selects a Bombardment target from a controlled ship. */
UCLASS(Blueprintable)
class CLASSFEATURE_API UGA_Bombardment : public UPlayerSkillGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Bombardment();

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

	/** Blueprint subclass holding radius, launch geometry, volley timing, projectile override, and preview class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bombardment")
	TSubclassOf<ABombardment> BombardmentClass;

private:
	AShip* FindRiddenShip() const;

	TWeakObjectPtr<AShip> ActiveShip;
	bool bAddedActivationTag = false;
};
