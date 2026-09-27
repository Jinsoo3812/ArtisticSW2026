#include "Effects/StatusGameplayEffect.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "StatusEffectLibrary.h"
#include "GameplayEffectComponents/CustomCanApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

bool UStatusApplicationRequirement::CanApplyGameplayEffect_Implementation(const UGameplayEffect* Effect,
	const FGameplayEffectSpec& Spec, UAbilitySystemComponent* ASC) const
{
	return UStatusEffectLibrary::CanApplyStatus(ASC, Spec, UStatusEffectLibrary::ResolveStatusTag(Spec));
}

UStatusGameplayEffect::UStatusGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(3.f);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::NeverRefresh;
	StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::NeverReset;
	bDenyOverflowApplication = true;
	bClearStackOnOverflow = false;
	auto* Requirements = CreateDefaultSubobject<UCustomCanApplyGameplayEffectComponent>(TEXT("StatusRequirements"));
	Requirements->ApplicationRequirements.Add(UStatusApplicationRequirement::StaticClass());
	GEComponents.Add(Requirements);
	GEComponents.Add(CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("StatusTags")));
}

void UStatusGameplayEffect::AddStatusTag(FGameplayTag Tag)
{
	auto& Component = *const_cast<UTargetTagsGameplayEffectComponent*>(FindComponent<UTargetTagsGameplayEffectComponent>());
	FInheritedTagContainer Tags = Component.GetConfiguredTargetTagChanges();
	Tags.AddTag(Tag);
	Component.SetAndApplyTargetTagChanges(Tags);
}

UStunGameplayEffect::UStunGameplayEffect()
{
	AddStatusTag(State_Status_Stun);
	AddStatusTag(State_Control_ActionsBlocked);
	AddStatusTag(State_Control_MovementBlocked);
	FGameplayEffectCue& Cue = GameplayCues.AddDefaulted_GetRef();
	Cue.GameplayCueTags.AddTag(GameplayCue_Status_Stun);
}

namespace
{
	void ConfigureDamageOverTime(UGameplayEffect& Effect)
	{
		Effect.DurationMagnitude = FScalableFloat(5.f);
		Effect.Period = FScalableFloat(1.f);
		Effect.bExecutePeriodicEffectOnApplication = false;
		FGameplayModifierInfo& Modifier = Effect.Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = UBaseAttributeSet::GetDamageAttribute();
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FScalableFloat(2.f);
	}
}

UPoisonStatusGameplayEffect::UPoisonStatusGameplayEffect()
{
	PeriodicDamageCueTag = GameplayCue_Status_Poison_Tick;
	AddStatusTag(State_Status_Poison);
	AddStatusTag(State_Poisoned);
	ConfigureDamageOverTime(*this);
}

UBurnStatusGameplayEffect::UBurnStatusGameplayEffect()
{
	PeriodicDamageCueTag = GameplayCue_Status_Burn_Tick;
	AddStatusTag(State_Status_Burn);
	ConfigureDamageOverTime(*this);
}
