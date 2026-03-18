#include "BaseGameplayTags.h"

// State
UE_DEFINE_GAMEPLAY_TAG(State_Attacking, "State.Attacking");
UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
UE_DEFINE_GAMEPLAY_TAG(State_Damaged, "State.Damaged");
UE_DEFINE_GAMEPLAY_TAG(State_Aiming, "State.Aiming");

// Team
UE_DEFINE_GAMEPLAY_TAG(Team_Player, "Team.Player");
UE_DEFINE_GAMEPLAY_TAG(Team_Enemy, "Team.Enemy");

// GameplayAbility
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Active, "GameplayAbility.Active");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Dead, "GameplayAbility.Dead");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_HitReaction, "GameplayAbility.HitReaction");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_TestHit, "GameplayAbility.TestHit");
// Event
UE_DEFINE_GAMEPLAY_TAG(Event_Ability_Changed, "Event.Ability.Changed");
// Data
UE_DEFINE_GAMEPLAY_TAG(Data_Damage, "Data.Damage");
UE_DEFINE_GAMEPLAY_TAG(Data_Heal, "Data.Heal");


/* Keyboard Input */

// ItemSlot
UE_DEFINE_GAMEPLAY_TAG(Key_Item, "Key.Item");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_1, "Key.Item.1");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_2, "Key.Item.2");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_3, "Key.Item.3");

// Crafter only
UE_DEFINE_GAMEPLAY_TAG(Key_Crafter_R, "Key.Crafter.R");


/* Item */
// Item 식별 Tag
UE_DEFINE_GAMEPLAY_TAG(Item_TestCrafted, "Item.TestCrafted");

UE_DEFINE_GAMEPLAY_TAG(Item_Weapon_Grenade, "Item.Weapon.Grenade");


/* Mouse Input */
// GameplayEvent로 사용할 Tag
UE_DEFINE_GAMEPLAY_TAG(Input_MouseLeftClick, "Input.MouseLeftClick");
UE_DEFINE_GAMEPLAY_TAG(Input_MouseRightClick, "Input.MouseRightClick");


/* Ability */
UE_DEFINE_GAMEPLAY_TAG(Ability_Item_Equipped, "Ability.Item.Equipped");


/* Feature Class */
UE_DEFINE_GAMEPLAY_TAG(Class_Crafter, "Class.Crafter");
UE_DEFINE_GAMEPLAY_TAG(Class_Attacker, "Class.Attacker");


/* Interaction */
UE_DEFINE_GAMEPLAY_TAG(Interaction, "Interaction");
UE_DEFINE_GAMEPLAY_TAG(Interaction_PickUp, "Interaction.PickUp");
UE_DEFINE_GAMEPLAY_TAG(Interaction_Craft, "Interaction.Craft");