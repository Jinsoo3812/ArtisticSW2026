#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/PlayerCombatGameplayAbility.h"
#include "PlayerSkillGameplayAbility.generated.h"

/**
 * Common activation gate for all player skills.
 * Unlock/resource availability is checked on activation, while the material is
 * consumed only by the derived skill at its actual execution point.
 */
UCLASS(Abstract)
class CLASSFEATURE_API UPlayerSkillGameplayAbility : public UPlayerCombatGameplayAbility
{
	GENERATED_BODY()

public:
	UPlayerSkillGameplayAbility();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill", meta = (Categories = "GameplayAbility.Skill"))
	FGameplayTag SkillTag;

	bool HasSkillUseAvailable() const;
	bool TryConsumeSkillUse() const;
};
