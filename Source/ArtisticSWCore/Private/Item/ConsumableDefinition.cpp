#include "Item/ConsumableDefinition.h"
#include "BaseGameplayTags.h"

bool FConsumableDefinition::IsValid() const
{
    return Effect > EConsumableEffect::Disabled && Effect <= EConsumableEffect::StrengthBuff
        && FMath::IsFinite(Healing) && Healing >= 0.f
        && FMath::IsFinite(Duration) && Duration > 0.f
        && FMath::IsFinite(Cooldown) && Cooldown > 0.f
        && FMath::IsFinite(StrengthBonus) && StrengthBonus >= 0.f
        && FMath::IsFinite(MoveMultiplier) && MoveMultiplier >= 1.f && MoveMultiplier <= 3.f
        && FMath::IsFinite(AttackMultiplier) && AttackMultiplier >= 1.f && AttackMultiplier <= 3.f
        && Rank >= 1 && Rank <= 4
        && ((Effect != EConsumableEffect::InstantHeal && Effect != EConsumableEffect::Regeneration) || Healing > 0.f)
        && (Effect != EConsumableEffect::Regeneration || Duration > 1.f)
        && (Effect != EConsumableEffect::MovementBuff || MoveMultiplier > 1.f)
        && (Effect != EConsumableEffect::StrengthBuff || StrengthBonus > 0.f || AttackMultiplier > 1.f);
}

bool FConsumableDefinition::IsRecovery() const
{
    return Effect == EConsumableEffect::InstantHeal || Effect == EConsumableEffect::Regeneration
        || Effect == EConsumableEffect::HealAndCleanse;
}

bool FConsumableDefinition::Resolve(FGameplayTag ItemTag, const FConsumableDefinition& Authored, FConsumableDefinition& Out)
{
    Out = Authored;
    if (Out.Effect != EConsumableEffect::NativeDefault) return Out.IsValid();
    Out = FConsumableDefinition();
    if (ItemTag == Item_Id_Consumables_Heal_Medicine)
    {
        Out.Effect = EConsumableEffect::InstantHeal;
    }
    else if (ItemTag == Item_Id_Consumables_Heal_Tangyak)
    {
        Out.Effect = EConsumableEffect::Regeneration;
        Out.Healing = 10.f;
        // Five ticks at seconds 1..5, with expiration after the last tick.
        Out.Duration = 5.1f;
        Out.Rank = 2;
    }
    else if (ItemTag == Item_Id_Consumables_Heal_Elixir || ItemTag == Item_Id_Consumables_Heal_Panacea)
    {
        const bool bPanacea = ItemTag == Item_Id_Consumables_Heal_Panacea;
        Out.Effect = bPanacea ? EConsumableEffect::HealAndCleanse : EConsumableEffect::InstantHeal;
        Out.Healing = bPanacea ? 100.f : 60.f;
        Out.Rank = bPanacea ? 4 : 3;
    }
    else if (ItemTag == Item_Id_Consumables_Buff_Doraji)
    {
        Out.Effect = EConsumableEffect::MovementBuff;
        Out.Duration = 20.f;
    }
    else if (ItemTag == Item_Id_Consumables_Buff_Chungshimhwan)
    {
        Out.Effect = EConsumableEffect::Cleanse;
        Out.Rank = 2;
    }
    else if (ItemTag == Item_Id_Consumables_Buff_Gongjindan || ItemTag == Item_Id_Consumables_Buff_RoyalGongjindan)
    {
        const bool bRoyal = ItemTag == Item_Id_Consumables_Buff_RoyalGongjindan;
        Out.Effect = EConsumableEffect::StrengthBuff;
        Out.Duration = 30.f;
        Out.StrengthBonus = bRoyal ? 10.f : 5.f;
        Out.AttackMultiplier = bRoyal ? 1.15f : 1.f;
        Out.Rank = bRoyal ? 4 : 3;
    }
    else return false;
    return Out.IsValid();
}
