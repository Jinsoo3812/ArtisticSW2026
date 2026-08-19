#include "GAS/Ability/Boss/GA_BossBasicAttack.h"

#include "BaseGameplayTags.h"

UGA_BossBasicAttack::UGA_BossBasicAttack()
{
	ActivationOwnedTags.AddTag(State_Boss_Busy);
	ActivationBlockedTags.AddTag(State_Boss_Busy);
	ActivationBlockedTags.AddTag(State_Boss_Hidden);
	ActivationBlockedTags.AddTag(State_Boss_Dashing);
	ActivationBlockedTags.AddTag(State_Dead);
}
