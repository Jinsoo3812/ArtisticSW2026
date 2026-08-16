#include "Skills/PlayerSkillGameplayAbility.h"

#include "BaseGameplayTags.h"
#include "Skills/SkillUseProvider.h"

UPlayerSkillGameplayAbility::UPlayerSkillGameplayAbility()
{
	ActivationBlockedTags.AddTag(State_Debuff_TimeStopped);
}

bool UPlayerSkillGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const ISkillUseProvider* Provider = Cast<ISkillUseProvider>(Avatar);
	return SkillTag.IsValid() && Provider && Provider->CanUseSkill(SkillTag);
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
