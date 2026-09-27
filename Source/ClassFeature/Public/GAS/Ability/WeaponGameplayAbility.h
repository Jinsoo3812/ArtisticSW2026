#pragma once
#include "CoreMinimal.h"
#include "GAS/Ability/PlayerCombatGameplayAbility.h"
#include "WeaponGameplayAbility.generated.h"

class ABaseItem;

/** A weapon action can only execute from its currently equipped source actor. */
UCLASS(Abstract)
class CLASSFEATURE_API UWeaponGameplayAbility : public UPlayerCombatGameplayAbility
{
	GENERATED_BODY()
public:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	ABaseItem* GetSourceWeapon() const;
};
