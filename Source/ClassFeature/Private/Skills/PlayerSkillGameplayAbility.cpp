#include "Skills/PlayerSkillGameplayAbility.h"
#include "Development/SWDevelopmentOptions.h"

#include "BaseGameplayTags.h"
#include "GameFramework/Actor.h"
#include "Skills/SkillUseProvider.h"
#include "WeaponInputAbilitySystemComponent.h"

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
	const bool bAllowed = SkillTag.IsValid() && Provider && Provider->CanUseSkill(SkillTag);
	if (!bAllowed && UWeaponInputAbilitySystemComponent::IsInputDiagnosticsEnabled())
	{
		UE_LOG(LogTemp, Display, TEXT("[SWCombatInput] Event=SkillUseRejected Avatar=%s Authority=%d Ability=%s Skill=%s Provider=%d DevMask=%d"),
			*GetNameSafe(Avatar), Avatar && Avatar->HasAuthority(), *GetName(), *SkillTag.ToString(), Provider != nullptr,
			SWDevelopmentOptions::GetMask(Avatar));
	}
	return bAllowed;
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
