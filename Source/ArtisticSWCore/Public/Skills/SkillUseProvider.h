#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SkillUseProvider.generated.h"

/**
 * Cross-module bridge used by skill execution actors (ship/cannon) to query and
 * consume a player's authoritative skill resource without depending on ClassFeature.
 */
UINTERFACE(MinimalAPI)
class USkillUseProvider : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API ISkillUseProvider
{
	GENERATED_BODY()

public:
	virtual bool CanUseSkill(const FGameplayTag& SkillTag) const = 0;
	virtual bool TryConsumeSkillUse(const FGameplayTag& SkillTag) = 0;
};
