#include "Effects/ConsumableGameplayEffects.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"

namespace ConsumableTags
{
    UE_DEFINE_GAMEPLAY_TAG(CooldownRecovery, "Cooldown.Item.Recovery");
    UE_DEFINE_GAMEPLAY_TAG(CooldownUtility, "Cooldown.Item.Utility");
    UE_DEFINE_GAMEPLAY_TAG(Regeneration, "State.Buff.Consumable.Regeneration");
    UE_DEFINE_GAMEPLAY_TAG(Movement, "State.Buff.Consumable.Movement");
    UE_DEFINE_GAMEPLAY_TAG(Strength, "State.Buff.Consumable.Strength");
}

namespace
{
    void AddModifier(UGameplayEffect& Effect, FGameplayAttribute Attribute, FGameplayTag MagnitudeTag, EGameplayModOp::Type Op)
    {
        FSetByCallerFloat Value;
        Value.DataTag = MagnitudeTag;
        FGameplayModifierInfo& Modifier = Effect.Modifiers.AddDefaulted_GetRef();
        Modifier.Attribute = Attribute;
        Modifier.ModifierOp = Op;
        Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Value);
    }
}

UConsumableHealingEffect::UConsumableHealingEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    AddModifier(*this, UBaseAttributeSet::GetHealingAttribute(), Data_Heal, EGameplayModOp::Additive);
    auto* Requirements = CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("LivingTarget"));
    Requirements->ApplicationTagRequirements.IgnoreTags.AddTag(State_Dead);
    Requirements->RemovalTagRequirements.RequireTags.AddTag(State_Dead);
    GEComponents.Add(Requirements);
}

UConsumableRegenerationEffect::UConsumableRegenerationEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(5.1f);
    Period = FScalableFloat(1.f);
    bExecutePeriodicEffectOnApplication = false;
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("RegenerationTag"));
    FInheritedTagContainer Granted;
    Granted.AddTag(ConsumableTags::Regeneration);
    Tags->SetAndApplyTargetTagChanges(Granted);
    GEComponents.Add(Tags);
}

UConsumableMovementEffect::UConsumableMovementEffect()
{
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    StackingType = EGameplayEffectStackingType::AggregateByTarget;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
    StackLimitCount = 1;
    StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
    bDenyOverflowApplication = false;
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(20.f);
    AddModifier(*this, UBaseAttributeSet::GetMoveSpeedMultiplierAttribute(), Data_Effect_MoveSpeedMultiplier, EGameplayModOp::Multiplicitive);
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("MovementTag"));
    FInheritedTagContainer Granted;
    Granted.AddTag(ConsumableTags::Movement);
    Tags->SetAndApplyTargetTagChanges(Granted);
    GEComponents.Add(Tags);
    auto* Requirements = CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("LivingTarget"));
    Requirements->RemovalTagRequirements.RequireTags.AddTag(State_Dead);
    GEComponents.Add(Requirements);
}

UConsumableStrengthEffect::UConsumableStrengthEffect()
{
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    StackingType = EGameplayEffectStackingType::AggregateByTarget;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
    StackLimitCount = 1;
    StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
    bDenyOverflowApplication = false;
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(30.f);
    AddModifier(*this, UBaseAttributeSet::GetStrengthAttribute(), Data_StrengthBonus, EGameplayModOp::Additive);
    AddModifier(*this, UBaseAttributeSet::GetAttackSpeedMultiplierAttribute(), Data_Effect_AttackSpeedMultiplier, EGameplayModOp::Multiplicitive);
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("StrengthTag"));
    FInheritedTagContainer Granted;
    Granted.AddTag(ConsumableTags::Strength);
    Tags->SetAndApplyTargetTagChanges(Granted);
    GEComponents.Add(Tags);
    auto* Requirements = CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("LivingTarget"));
    Requirements->RemovalTagRequirements.RequireTags.AddTag(State_Dead);
    GEComponents.Add(Requirements);
}

UConsumableRecoveryCooldownEffect::UConsumableRecoveryCooldownEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(5.f);
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTag"));
    FInheritedTagContainer Granted;
    Granted.AddTag(ConsumableTags::CooldownRecovery);
    Tags->SetAndApplyTargetTagChanges(Granted);
    GEComponents.Add(Tags);
}

UConsumableUtilityCooldownEffect::UConsumableUtilityCooldownEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(5.f);
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTag"));
    FInheritedTagContainer Granted;
    Granted.AddTag(ConsumableTags::CooldownUtility);
    Tags->SetAndApplyTargetTagChanges(Granted);
    GEComponents.Add(Tags);
}
