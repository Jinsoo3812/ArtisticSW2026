#include "BaseGameplayTags.h"

// State
UE_DEFINE_GAMEPLAY_TAG(State_Attacking, "State.Attacking");
UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
UE_DEFINE_GAMEPLAY_TAG(State_Damaged, "State.Damaged");
UE_DEFINE_GAMEPLAY_TAG(State_Aiming, "State.Aiming");
UE_DEFINE_GAMEPLAY_TAG(State_Sniping, "State.Sniping");
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
UE_DEFINE_GAMEPLAY_TAG(Event_Montage_ThrowGrenade, "Event.Montage.ThrowGrenade");
UE_DEFINE_GAMEPLAY_TAG(Event_Montage_FireArrow, "Event.Montage.FireArrow");
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
UE_DEFINE_GAMEPLAY_TAG(Item_Weapon_Bow, "Item.Weapon.Bow");

// Tool
UE_DEFINE_GAMEPLAY_TAG(Item_Tool, "Item.Tool");
UE_DEFINE_GAMEPLAY_TAG(Item_Tool_Grenade, "Item.Tool.Grenade");
UE_DEFINE_GAMEPLAY_TAG(Item_Tool_TestRed, "Item.Tool.TestRed");
UE_DEFINE_GAMEPLAY_TAG(Item_Tool_TestBlue, "Item.Tool.TestBlue");
UE_DEFINE_GAMEPLAY_TAG(Item_Tool_Trap, "Item.Tool.Trap");
UE_DEFINE_GAMEPLAY_TAG(Item_Tool_ClusterGranade, "Item.Tool.ClusterGranade");
UE_DEFINE_GAMEPLAY_TAG(Item_Tool_SniperRifle, "Item.Tool.SniperRifle");


// Material
UE_DEFINE_GAMEPLAY_TAG(Item_Material, "Item.Material");
UE_DEFINE_GAMEPLAY_TAG(Item_Material_Ore, "Item.Material.Ore");
UE_DEFINE_GAMEPLAY_TAG(Item_Material_Cloths, "Item.Material.Cloths");
UE_DEFINE_GAMEPLAY_TAG(Item_Material_Woods, "Item.Material.Woods");
UE_DEFINE_GAMEPLAY_TAG(Item_Material_Submunition, "Item.Material.Submunition");
UE_DEFINE_GAMEPLAY_TAG(Item_Material_Submunition_Explosive, "Item.Material.Submunition.Explosive");
UE_DEFINE_GAMEPLAY_TAG(Item_Material_Submunition_Trap, "Item.Material.Submunition.Trap");

// Enemy
UE_DEFINE_GAMEPLAY_TAG(Item_EnemyWeapon_Sword, "Item.EnemyWeapon.Sword");

// Enemy Type
// 적 구분 태그
UE_DEFINE_GAMEPLAY_TAG(Enemy_Animal_Test, "Enemy.Type.Animal.Test");
UE_DEFINE_GAMEPLAY_TAG(Enemy_Undead_Test, "Enemy.Type.Undead.Test");


/* Default - Keyboard & Mouse */
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse, "Key.Default.Mouse");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_LeftClick, "Key.Default.Mouse.LeftClick");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_LeftClick_Released, "Key.Default.Mouse.LeftClick.Released");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_RightClick, "Key.Default.Mouse.RightClick");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_RightClick_Released, "Key.Default.Mouse.RightClick.Released");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_WheelUp, "Key.Default.Mouse.WheelUp");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Mouse_WheelDown, "Key.Default.Mouse.WheelDown");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_F, "Key.Default.F");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_ESC, "Key.Default.ESC");
UE_DEFINE_GAMEPLAY_TAG(Key_Default_Space, "Key.Default.Space");

/* UI Input */
// Ex. I >> Inventory, M >> Map, E >> Equipment
UE_DEFINE_GAMEPLAY_TAG(Key_UI_I, "Key.UI.I");

/* Ability */
UE_DEFINE_GAMEPLAY_TAG(Ability_Item_Equipped, "Ability.Item.Equipped");

/* Feature Class */
UE_DEFINE_GAMEPLAY_TAG(Class_Crafter, "Class.Crafter");
UE_DEFINE_GAMEPLAY_TAG(Class_Attacker, "Class.Attacker");


/* Interaction */
UE_DEFINE_GAMEPLAY_TAG(Interaction, "Interaction");
UE_DEFINE_GAMEPLAY_TAG(Interaction_PickUp, "Interaction.PickUp");
UE_DEFINE_GAMEPLAY_TAG(Interaction_Craft, "Interaction.Craft");
UE_DEFINE_GAMEPLAY_TAG(Interaction_ShipBoard, "Interaction.ShipBoard");
UE_DEFINE_GAMEPLAY_TAG(Interaction_CannonBoard, "Interaction.CannonBoard");
