#include "BaseGameplayTags.h"

// State
UE_DEFINE_GAMEPLAY_TAG(State_Attacking, "State.Attacking");
UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
UE_DEFINE_GAMEPLAY_TAG(State_Damaged, "State.Damaged");
// Team
UE_DEFINE_GAMEPLAY_TAG(Team_Player, "Team.Player");
UE_DEFINE_GAMEPLAY_TAG(Team_Enemy, "Team.Enemy");
// GameplayAbility
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Active, "GameplayAbility.Active");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Dead, "GameplayAbility.Dead");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_HitReaction, "GameplayAbility.HitReaction");
// Event
UE_DEFINE_GAMEPLAY_TAG(Event_Ability_Changed, "Event.Ability.Changed");