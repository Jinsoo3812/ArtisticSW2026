#include "Skills/PlayerSkillGameplayAbility.h"
#include "Development/SWDevelopmentOptions.h"

#include "BaseGameplayTags.h"
#include "GameFramework/Actor.h"
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

bool UPlayerSkillGameplayAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle,
 const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
 return SWDevelopmentOptions::IsEnabled(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, SWDevelopmentOptions::SkillNoCooldown)
  || Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
}

void UPlayerSkillGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
 const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
 if (!SWDevelopmentOptions::IsEnabled(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr, SWDevelopmentOptions::SkillNoCooldown))
  Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
}
