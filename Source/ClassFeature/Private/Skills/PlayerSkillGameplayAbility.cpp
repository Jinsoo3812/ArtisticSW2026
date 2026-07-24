#include "Skills/PlayerSkillGameplayAbility.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Skills/SkillUseProvider.h"
#include "Skills/PlayerSkillComponent.h"

bool UPlayerSkillGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		if (SkillTag.MatchesTagExact(GameplayAbility_Skill_GravityVortex))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[GravityVortex][CanActivate] Super rejected. Avatar=%s Owner=%s RelevantTags=%s"),
				*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
				*GetNameSafe(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr),
				OptionalRelevantTags ? *OptionalRelevantTags->ToStringSimple() : TEXT("None"));
		}
		return false;
	}

	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const ISkillUseProvider* Provider = Cast<ISkillUseProvider>(Avatar);
	const bool bCanUse = SkillTag.IsValid() && Provider && Provider->CanUseSkill(SkillTag);
	if (SkillTag.MatchesTagExact(GameplayAbility_Skill_GravityVortex))
	{
		const ABasePlayer* Player = Cast<ABasePlayer>(Avatar);
		const UPlayerSkillComponent* SkillComponent = Player ? Player->GetPlayerSkillComponent() : nullptr;
		UE_LOG(LogTemp, Warning,
			TEXT("[GravityVortex][CanActivate] Result=%s Avatar=%s Provider=%s Bypass=%s Unlocked=%s Uses=%d"),
			bCanUse ? TEXT("true") : TEXT("false"),
			*GetNameSafe(Avatar),
			Provider ? TEXT("true") : TEXT("false"),
			Player && Player->bBypassSkillRequirementsForTesting ? TEXT("true") : TEXT("false"),
			SkillComponent && SkillComponent->IsSkillUnlocked(SkillTag) ? TEXT("true") : TEXT("false"),
			SkillComponent ? SkillComponent->GetSkillUseCount(SkillTag) : INDEX_NONE);
	}
	return bCanUse;
}

bool UPlayerSkillGameplayAbility::HasSkillUseAvailable() const
{
	const ISkillUseProvider* Provider = Cast<ISkillUseProvider>(GetAvatarActorFromActorInfo());
	return SkillTag.IsValid() && Provider && Provider->CanUseSkill(SkillTag);
}

bool UPlayerSkillGameplayAbility::TryConsumeSkillUse() const
{
	ISkillUseProvider* Provider = Cast<ISkillUseProvider>(GetAvatarActorFromActorInfo());
	return SkillTag.IsValid() && Provider && Provider->TryConsumeSkillUse(SkillTag);
}
