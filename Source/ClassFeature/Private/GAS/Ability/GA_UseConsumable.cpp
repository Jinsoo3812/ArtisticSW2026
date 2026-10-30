#include "GAS/Ability/GA_UseConsumable.h"
#include "BasePlayer.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemSubsystem.h"
#include "Item/ItemData.h"
#include "Effects/ConsumableGameplayEffects.h"
#include "Engine/World.h"

namespace
{
    TArray<FActiveGameplayEffectHandle> FindCleanseTargets(UAbilitySystemComponent* ASC)
    {
        FGameplayTagContainer Tags;
        Tags.AddTag(State_Status_Poison);
        Tags.AddTag(State_Status_Burn);
        return ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags));
    }

    FGameplayTag GetFamily(const FConsumableDefinition& Definition)
    {
        switch (Definition.Effect)
        {
        case EConsumableEffect::Regeneration: return ConsumableTags::Regeneration;
        case EConsumableEffect::MovementBuff: return ConsumableTags::Movement;
        case EConsumableEffect::StrengthBuff: return ConsumableTags::Strength;
        default: return FGameplayTag();
        }
    }
}

UGA_UseConsumable::UGA_UseConsumable()
{
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationBlockedTags.AddTag(State_Dead);
    ActivationBlockedTags.AddTag(State_Debuff_TimeStopped);
}

bool UGA_UseConsumable::ResolveUse(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, FConsumableDefinition& Out) const
{
    const ABasePlayer* Player = ActorInfo ? Cast<ABasePlayer>(ActorInfo->AvatarActor.Get()) : nullptr;
    UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
    const UConsumableUseRequest* Request = Cast<UConsumableUseRequest>(GetSourceObject(Handle, ActorInfo));
    if (!Player || !Player->HasAuthority() || !Player->CanPerformCombatAction() || !ASC || !Request
        || !Player->GetInventoryComponent() || Player->GetInventoryComponent()->IsItemUseInProgress()
        || Player->GetInventoryComponent()->GetItemCount(Request->ItemTag) < 1) return false;
    UItemSubsystem* Items = Player->GetWorld()->GetSubsystem<UItemSubsystem>();
    const FItemDefinition* Item = Items ? Items->GetItemDefinition(Request->ItemTag) : nullptr;
    if (!Item || !Items->GetCategoryTag(Request->ItemTag).MatchesTag(Item_Category_Consumable)
        || !FConsumableDefinition::Resolve(Request->ItemTag, Item->Consumable, Out)) return false;
    if (ASC->HasMatchingGameplayTag(Out.IsRecovery() ? ConsumableTags::CooldownRecovery : ConsumableTags::CooldownUtility)
        || !ASC->GetSet<UBaseAttributeSet>()
        || ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) <= 0.f) return false;
    const bool bNeedsHealth = ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute())
        < ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute());
    const bool bHasCleanseTarget = !FindCleanseTargets(ASC).IsEmpty();
    if ((Out.Effect == EConsumableEffect::InstantHeal || Out.Effect == EConsumableEffect::Regeneration) && !bNeedsHealth) return false;
    if (Out.Effect == EConsumableEffect::Cleanse && !bHasCleanseTarget) return false;
    if (Out.Effect == EConsumableEffect::HealAndCleanse && !(bNeedsHealth && Out.Healing > 0.f) && !bHasCleanseTarget) return false;
    const FGameplayTag Family = GetFamily(Out);
    if (Family.IsValid())
    {
        for (const FActiveGameplayEffectHandle ActiveHandle : ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAllOwningTags(FGameplayTagContainer(Family))))
        {
            const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(ActiveHandle);
            if (Active && (Out.Effect == EConsumableEffect::Regeneration || Active->Spec.GetLevel() > Out.Rank)) return false;
        }
    }
    return true;
}

bool UGA_UseConsumable::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    FConsumableDefinition Definition;
    return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)
        && ResolveUse(Handle, ActorInfo, Definition);
}

void UGA_UseConsumable::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
    FConsumableDefinition Definition;
    UConsumableUseRequest* Request = Cast<UConsumableUseRequest>(GetSourceObject(Handle, ActorInfo));
    bool bSucceeded = false;
    if (Request && ResolveUse(Handle, ActorInfo, Definition))
    {
        ABasePlayer* Player = CastChecked<ABasePlayer>(ActorInfo->AvatarActor.Get());
        UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
        bSucceeded = Player->GetInventoryComponent()->TryConsumeItemWithEffect(Request->ItemTag,
            [this, ASC, Definition]() { return ApplyConsumable(ASC, Definition); });
        Request->bSucceeded = bSucceeded;
    }
    EndAbility(Handle, ActorInfo, ActivationInfo, true, !bSucceeded);
}

bool UGA_UseConsumable::ApplyConsumable(UAbilitySystemComponent* ASC, const FConsumableDefinition& Definition)
{
    auto MakeSpec = [ASC, &Definition](TSubclassOf<UGameplayEffect> Class, float Duration)
    {
        FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Class, Definition.Rank, ASC->MakeEffectContext());
        if (Spec.IsValid())
        {
            if (Duration > 0.f) Spec.Data->SetDuration(Duration, true);
            Spec.Data->SetSetByCallerMagnitude(Data_Heal, Definition.Healing);
            Spec.Data->SetSetByCallerMagnitude(Data_StrengthBonus, Definition.StrengthBonus);
            Spec.Data->SetSetByCallerMagnitude(Data_Effect_MoveSpeedMultiplier, Definition.MoveMultiplier);
            Spec.Data->SetSetByCallerMagnitude(Data_Effect_AttackSpeedMultiplier, Definition.AttackMultiplier);
        }
        return Spec;
    };
    const FGameplayEffectSpecHandle CooldownSpec = MakeSpec(Definition.IsRecovery()
        ? UConsumableRecoveryCooldownEffect::StaticClass() : UConsumableUtilityCooldownEffect::StaticClass(), Definition.Cooldown);
    if (!CooldownSpec.IsValid()) return false;
    // Install cooldown first: a rejected cooldown can never grant a free benefit.
    const FActiveGameplayEffectHandle Cooldown = ASC->ApplyGameplayEffectSpecToSelf(*CooldownSpec.Data.Get());
    if (!Cooldown.IsValid()) return false;
    if (ASC->HasMatchingGameplayTag(State_Dead)
        || ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) <= 0.f)
    {
        ASC->RemoveActiveGameplayEffect(Cooldown);
        return false;
    }

    bool bApplied = false;
    const FGameplayTag Family = GetFamily(Definition);
    if (Family.IsValid())
    {
        const TArray<FActiveGameplayEffectHandle> Previous = ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAllOwningTags(FGameplayTagContainer(Family)));
        TSubclassOf<UGameplayEffect> Class = UConsumableRegenerationEffect::StaticClass();
        if (Definition.Effect == EConsumableEffect::MovementBuff) Class = UConsumableMovementEffect::StaticClass();
        if (Definition.Effect == EConsumableEffect::StrengthBuff) Class = UConsumableStrengthEffect::StaticClass();
        const FGameplayEffectSpecHandle Spec = MakeSpec(Class, Definition.Duration);
        if (Spec.IsValid())
        {
            const FActiveGameplayEffectHandle Applied = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            bApplied = Applied.IsValid();
            if (bApplied && Previous.Contains(Applied))
            {
                // UE 5.7 does not refresh attribute aggregators when stack count stays at one.
                // Explicitly recalculate the new SetByCaller values on an in-place upgrade/refresh.
                ASC->UpdateActiveGameplayEffectSetByCallerMagnitudes(Applied, Spec.Data->SetByCallerTagMagnitudes);
            }
            // Keep the previous effect when the replacement is rejected.
            if (bApplied) for (const FActiveGameplayEffectHandle Old : Previous)
                if (Old != Applied) ASC->RemoveActiveGameplayEffect(Old);
        }
    }
    else
    {
        if ((Definition.Effect == EConsumableEffect::InstantHeal || Definition.Effect == EConsumableEffect::HealAndCleanse)
            && Definition.Healing > 0.f
            && ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) < ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute()))
        {
            const FGameplayEffectSpecHandle Spec = MakeSpec(UConsumableHealingEffect::StaticClass(), 0.f);
            if (Spec.IsValid())
            {
                // Match the resolved execution, even if another health listener immediately deals damage.
                UBaseAttributeSet* Attributes = const_cast<UBaseAttributeSet*>(ASC->GetSet<UBaseAttributeSet>());
                const FGameplayEffectContext* Expected = Spec.Data->GetContext().Get();
                const FDelegateHandle Resolved = Attributes->OnHealingResolved.AddLambda(
                    [&bApplied, Expected](const FGameplayEffectContextHandle& Context, float Amount)
                    {
                        if (Context.Get() == Expected && Amount > 0.f) bApplied = true;
                    });
                ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
                Attributes->OnHealingResolved.Remove(Resolved);
            }
        }
        if (Definition.Effect == EConsumableEffect::Cleanse || Definition.Effect == EConsumableEffect::HealAndCleanse)
        {
            for (const FActiveGameplayEffectHandle Target : FindCleanseTargets(ASC))
                bApplied = ASC->RemoveActiveGameplayEffect(Target) || bApplied;
        }
    }
    if (!bApplied) ASC->RemoveActiveGameplayEffect(Cooldown);
    return bApplied;
}
