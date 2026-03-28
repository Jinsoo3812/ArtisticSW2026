#include "BaseGameplayTags.h"

// State
UE_DEFINE_GAMEPLAY_TAG(State_Attacking, "State.Attacking");
UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
UE_DEFINE_GAMEPLAY_TAG(State_Damaged, "State.Damaged");
UE_DEFINE_GAMEPLAY_TAG(State_Aiming, "State.Aiming");
UE_DEFINE_GAMEPLAY_TAG(State_Crafting, "State.Crafting");

// Team
UE_DEFINE_GAMEPLAY_TAG(Team_Player, "Team.Player");
UE_DEFINE_GAMEPLAY_TAG(Team_Enemy, "Team.Enemy");

// GameplayAbility
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Active, "GameplayAbility.Active");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Dead, "GameplayAbility.Dead");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_BasicAttack, "GameplayAbility.BasicAttack");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_HitReaction, "GameplayAbility.HitReaction");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_TestHit, "GameplayAbility.TestHit");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Equip, "GameplayAbility.Equip");
// Event
UE_DEFINE_GAMEPLAY_TAG(Event_Ability_Changed, "Event.Ability.Changed");
UE_DEFINE_GAMEPLAY_TAG(Event_HandleScan_Start, "Event.HandleScan.Start");
UE_DEFINE_GAMEPLAY_TAG(Event_HandleScan_End, "Event.HandleScan.End");
UE_DEFINE_GAMEPLAY_TAG(Event_ActivateAbility_Equip, "Event.ActivateAbility.Equip");
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
UE_DEFINE_GAMEPLAY_TAG(Item_Weapon_Sword, "Item.Weapon.Sword");

// Enemy
UE_DEFINE_GAMEPLAY_TAG(Item_EnemyWeapon_Sword, "Item.EnemyWeapon.Sword");

/* Mouse Input */


/* Key Input */
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse, "Key.Default.Mouse");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_LeftClick, "Key.Default.Mouse.LeftClick");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_RightClick, "Key.Default.Mouse.RightClick");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_F, "Key.Default.F");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_ESC, "Key.Default.ESC");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Space, "Key.Default.Space");


/* Feature Class */
UE_DEFINE_GAMEPLAY_TAG(Class_Crafter, "Class.Crafter");
UE_DEFINE_GAMEPLAY_TAG(Class_Attacker, "Class.Attacker");


/* Interaction */
UE_DEFINE_GAMEPLAY_TAG(Interaction, "Interaction");
UE_DEFINE_GAMEPLAY_TAG(Interaction_PickUp, "Interaction.PickUp");
UE_DEFINE_GAMEPLAY_TAG(Interaction_Craft, "Interaction.Craft");