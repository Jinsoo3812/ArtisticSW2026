#include "BaseGameplayTags.h"

// State
UE_DEFINE_GAMEPLAY_TAG(State_Attacking, "State.Attacking");
UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
UE_DEFINE_GAMEPLAY_TAG(State_Damaged, "State.Damaged");
UE_DEFINE_GAMEPLAY_TAG(State_Aiming, "State.Aiming");
UE_DEFINE_GAMEPLAY_TAG(State_Sniping, "State.Sniping");
UE_DEFINE_GAMEPLAY_TAG(State_Crafting, "State.Crafting");
UE_DEFINE_GAMEPLAY_TAG(State_Dialogue, "State.Dialogue");
UE_DEFINE_GAMEPLAY_TAG(State_Poisoned, "State.Poisoned");
UE_DEFINE_GAMEPLAY_TAG(State_Bow_Drawing, "State.Bow.Drawing");
UE_DEFINE_GAMEPLAY_TAG(State_Bow_FullyDrawn, "State.Bow.FullyDrawn");
UE_DEFINE_GAMEPLAY_TAG(State_Bow_Releasing, "State.Bow.Releasing");
UE_DEFINE_GAMEPLAY_TAG(State_Ship_CannonDisabled, "State.Ship.CannonDisabled");
UE_DEFINE_GAMEPLAY_TAG(State_Debuff_WaterBomb, "State.Debuff.WaterBomb");
UE_DEFINE_GAMEPLAY_TAG(State_Debuff_TimeStopped, "State.Debuff.TimeStopped");
UE_DEFINE_GAMEPLAY_TAG(AI_State_Boss_Intro, "AI.State.Boss.Intro");
UE_DEFINE_GAMEPLAY_TAG(AI_State_Boss_Combat, "AI.State.Boss.Combat");
UE_DEFINE_GAMEPLAY_TAG(AI_State_Boss_Dead, "AI.State.Boss.Dead");
UE_DEFINE_GAMEPLAY_TAG(State_Boss_Busy, "State.Boss.Busy");
UE_DEFINE_GAMEPLAY_TAG(State_Boss_Hidden, "State.Boss.Hidden");
UE_DEFINE_GAMEPLAY_TAG(State_Boss_Dashing, "State.Boss.Dashing");
UE_DEFINE_GAMEPLAY_TAG(State_CrowdControl_Knockback, "State.CrowdControl.Knockback");

// Team
UE_DEFINE_GAMEPLAY_TAG(Team_Player, "Team.Player");
UE_DEFINE_GAMEPLAY_TAG(Team_Enemy, "Team.Enemy");

// GameplayAbility
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Active, "GameplayAbility.Active");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Dead, "GameplayAbility.Dead");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_BasicAttack, "GameplayAbility.BasicAttack");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_RangedAttack, "GameplayAbility.RangedAttack");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_HitReaction, "GameplayAbility.HitReaction");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_InterruptibleByHit, "GameplayAbility.InterruptibleByHit");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_TestHit, "GameplayAbility.TestHit");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Equip, "GameplayAbility.Equip");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Weapon_AimCycle, "GameplayAbility.Weapon.AimCycle");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Skill_GravityVortex, "GameplayAbility.Skill.GravityVortex");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Skill_WaterBomb, "GameplayAbility.Skill.WaterBomb");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Skill_Bombardment, "GameplayAbility.Skill.Bombardment");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_EnemyShip_Charge, "GameplayAbility.EnemyShip.Charge");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_EnemyShip_LaunchTorpedo, "GameplayAbility.EnemyShip.LaunchTorpedo");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_EnemyShip_CannonVolley, "GameplayAbility.EnemyShip.CannonVolley");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_EnemyShip_DeployObstacle, "GameplayAbility.EnemyShip.DeployObstacle");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_EnemyShip_TimeStop, "GameplayAbility.EnemyShip.TimeStop");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Boss_Knockback, "GameplayAbility.Boss.Knockback");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Boss_Vanish, "GameplayAbility.Boss.Vanish");
UE_DEFINE_GAMEPLAY_TAG(GameplayAbility_Boss_DashSlash, "GameplayAbility.Boss.DashSlash");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_Enemy_BasicAttack, "Cooldown.Enemy.BasicAttack");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_Boss_Knockback, "Cooldown.Boss.Knockback");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_Boss_Vanish, "Cooldown.Boss.Vanish");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_Boss_DashSlash, "Cooldown.Boss.DashSlash");
UE_DEFINE_GAMEPLAY_TAG(State_EnemyShip_Charging, "State.EnemyShip.Charging");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_EnemyShip_Charge, "Cooldown.EnemyShip.Charge");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_EnemyShip_LaunchTorpedo, "Cooldown.EnemyShip.LaunchTorpedo");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_EnemyShip_DeployObstacle, "Cooldown.EnemyShip.DeployObstacle");
UE_DEFINE_GAMEPLAY_TAG(Cooldown_EnemyShip_TimeStop, "Cooldown.EnemyShip.TimeStop");
// GameplayCue
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Boss_Attack, "GameplayCue.Boss.Attack");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Boss_Hit, "GameplayCue.Boss.Hit");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Impact, "GameplayCue.Impact");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Impact_Weapon_Hand, "GameplayCue.Impact.Weapon.Hand");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Impact_Weapon_Sword, "GameplayCue.Impact.Weapon.Sword");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Impact_Weapon_Bow, "GameplayCue.Impact.Weapon.Bow");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Impact_Boss_DashSlash, "GameplayCue.Impact.Boss.DashSlash");
UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Impact_Boss_Knockback, "GameplayCue.Impact.Boss.Knockback");
// Event
UE_DEFINE_GAMEPLAY_TAG(Event_Ability_Changed, "Event.Ability.Changed");
UE_DEFINE_GAMEPLAY_TAG(Event_HandleScan_Start, "Event.HandleScan.Start");
UE_DEFINE_GAMEPLAY_TAG(Event_HandleScan_Tick, "Event.HandleScan.Tick");
UE_DEFINE_GAMEPLAY_TAG(Event_HandleScan_End, "Event.HandleScan.End");
UE_DEFINE_GAMEPLAY_TAG(Event_Attack_ComboCommit, "Event.Attack.Combo.Commit");
UE_DEFINE_GAMEPLAY_TAG(Event_Boss_Dash_Start, "Event.Boss.Dash.Start");
UE_DEFINE_GAMEPLAY_TAG(Event_Boss_Dash_SlashFinished, "Event.Boss.Dash.SlashFinished");
UE_DEFINE_GAMEPLAY_TAG(Event_ActivateAbility_Equip, "Event.ActivateAbility.Equip");
UE_DEFINE_GAMEPLAY_TAG(Event_Montage_ThrowGrenade, "Event.Montage.ThrowGrenade");
UE_DEFINE_GAMEPLAY_TAG(Event_Montage_FireArrow, "Event.Montage.FireArrow");
// Data
UE_DEFINE_GAMEPLAY_TAG(Data_Damage, "Data.Damage");
UE_DEFINE_GAMEPLAY_TAG(Data_Heal, "Data.Heal");
UE_DEFINE_GAMEPLAY_TAG(Data_AttackCoefficient, "Data.AttackCoefficient");
UE_DEFINE_GAMEPLAY_TAG(Data_ChargeMultiplier, "Data.ChargeMultiplier");
UE_DEFINE_GAMEPLAY_TAG(Data_StrengthBonus, "Data.StrengthBonus");
UE_DEFINE_GAMEPLAY_TAG(Data_Effect_AttackSpeedMultiplier, "Data.Effect.AttackSpeedMultiplier");


/* Keyboard Input */

// ItemSlot
UE_DEFINE_GAMEPLAY_TAG(Key_Item, "Key.Item");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_1, "Key.Item.1");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_2, "Key.Item.2");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_3, "Key.Item.3");
UE_DEFINE_GAMEPLAY_TAG(Key_Skill_GravityVortex, "Key.Skill.GravityVortex");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_4, "Key.Item.4");
UE_DEFINE_GAMEPLAY_TAG(Key_Item_5, "Key.Item.5");

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

// New Item Id
UE_DEFINE_GAMEPLAY_TAG(Item_Id, "Item.Id");

UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material, "Item.Id.Material");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponMaterial, "Item.Id.Material.WeaponMaterial");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponMaterial_Wood, "Item.Id.Material.WeaponMaterial.Wood");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponMaterial_GoodWood, "Item.Id.Material.WeaponMaterial.GoodWood");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponMaterial_Iron, "Item.Id.Material.WeaponMaterial.Iron");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponMaterial_GoodIron, "Item.Id.Material.WeaponMaterial.GoodIron");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ConsumablesMaterial, "Item.Id.Material.ConsumablesMaterial");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ConsumablesMaterial_Pear, "Item.Id.Material.ConsumablesMaterial.Pear");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ConsumablesMaterial_GoodPear, "Item.Id.Material.ConsumablesMaterial.GoodPear");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ConsumablesMaterial_Herbs, "Item.Id.Material.ConsumablesMaterial.Herbs");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ConsumablesMaterial_GoodHerbs, "Item.Id.Material.ConsumablesMaterial.GoodHerbs");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ConsumablesMaterial_OxGallStone, "Item.Id.Material.ConsumablesMaterial.OxGallStone");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials, "Item.Id.Material.ShipMaterials");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials_WoodenPlank, "Item.Id.Material.ShipMaterials.WoodenPlank");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials_GoodWoodenPlank, "Item.Id.Material.ShipMaterials.GoodWoodenPlank");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials_IronPlate, "Item.Id.Material.ShipMaterials.IronPlate");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials_GoodIronPlate, "Item.Id.Material.ShipMaterials.GoodIronPlate");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials_GrapplingHook, "Item.Id.Material.ShipMaterials.GrapplingHook");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_ShipMaterials_LuminousPearl, "Item.Id.Material.ShipMaterials.LuminousPearl");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponSpecialMaterial, "Item.Id.Material.WeaponSpecialMaterial");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponSpecialMaterial_EpicMaterial, "Item.Id.Material.WeaponSpecialMaterial.EpicMaterial");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial, "Item.Id.Material.WeaponSpecialMaterial.LegendaryMaterial");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponSpecialRecipe, "Item.Id.Material.WeaponSpecialRecipe");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponSpecialRecipe_EpicRecipe, "Item.Id.Material.WeaponSpecialRecipe.EpicRecipe");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_WeaponSpecialRecipe_LegendaryRecipe, "Item.Id.Material.WeaponSpecialRecipe.LegendaryRecipe");

UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_SkillMaterial, "Item.Id.Material.SkillMaterial");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_SkillMaterial_RareSkill, "Item.Id.Material.SkillMaterial.RareSkill");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_SkillMaterial_EpicSkill, "Item.Id.Material.SkillMaterial.EpicSkill");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_SkillMaterial_LegendarySkill, "Item.Id.Material.SkillMaterial.LegendarySkill");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_Etc, "Item.Id.Material.Etc");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Material_Etc_Gunpowder, "Item.Id.Material.Etc.Gunpowder");

UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables, "Item.Id.Consumables");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Heal, "Item.Id.Consumables.Heal");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Heal_Medicine, "Item.Id.Consumables.Heal.Medicine");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Heal_Tangyak, "Item.Id.Consumables.Heal.Tangyak");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Heal_Elixir, "Item.Id.Consumables.Heal.Elixir");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Buff, "Item.Id.Consumables.Buff");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Buff_Doraji, "Item.Id.Consumables.Buff.Doraji");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Buff_Chungshimhwan, "Item.Id.Consumables.Buff.Chungshimhwan");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Consumables_Buff_Gongjindan, "Item.Id.Consumables.Buff.Gongjindan");

UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon, "Item.Id.Weapon");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword, "Item.Id.Weapon.Sword");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordA1, "Item.Id.Weapon.Sword.SwordA1");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordA2, "Item.Id.Weapon.Sword.SwordA2");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordA3, "Item.Id.Weapon.Sword.SwordA3");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordA4, "Item.Id.Weapon.Sword.SwordA4");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordA5, "Item.Id.Weapon.Sword.SwordA5");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordB1, "Item.Id.Weapon.Sword.SwordB1");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordB2, "Item.Id.Weapon.Sword.SwordB2");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordB3, "Item.Id.Weapon.Sword.SwordB3");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordB4, "Item.Id.Weapon.Sword.SwordB4");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Sword_SwordB5, "Item.Id.Weapon.Sword.SwordB5");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow, "Item.Id.Weapon.Bow");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_ShortBow1, "Item.Id.Weapon.Bow.ShortBow1");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_ShortBow2, "Item.Id.Weapon.Bow.ShortBow2");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_ShortBow3, "Item.Id.Weapon.Bow.ShortBow3");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_ShortBow4, "Item.Id.Weapon.Bow.ShortBow4");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_ShortBow5, "Item.Id.Weapon.Bow.ShortBow5");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_LongBow1, "Item.Id.Weapon.Bow.LongBow1");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_LongBow2, "Item.Id.Weapon.Bow.LongBow2");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_LongBow3, "Item.Id.Weapon.Bow.LongBow3");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_LongBow4, "Item.Id.Weapon.Bow.LongBow4");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Weapon_Bow_LongBow5, "Item.Id.Weapon.Bow.LongBow5");

UE_DEFINE_GAMEPLAY_TAG(Item_Id_Skill, "Item.Id.Skill");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Skill_GravityVortex, "Item.Id.Skill.GravityVortex");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Skill_WaterBomb, "Item.Id.Skill.WaterBomb");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Skill_Bombardment, "Item.Id.Skill.Bombardment");

UE_DEFINE_GAMEPLAY_TAG(Item_Id_Clue, "Item.Id.Clue");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Clue_Clue1, "Item.Id.Clue.Clue1");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Clue_Clue2, "Item.Id.Clue.Clue2");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Clue_Clue3, "Item.Id.Clue.Clue3");
UE_DEFINE_GAMEPLAY_TAG(Item_Id_Clue_Clue4, "Item.Id.Clue.Clue4");

// Item Rarity
UE_DEFINE_GAMEPLAY_TAG(Item_Rarity, "Item.Rarity");
UE_DEFINE_GAMEPLAY_TAG(Item_Rarity_Common, "Item.Rarity.Common");
UE_DEFINE_GAMEPLAY_TAG(Item_Rarity_Rare, "Item.Rarity.Rare");
UE_DEFINE_GAMEPLAY_TAG(Item_Rarity_Epic, "Item.Rarity.Epic");
UE_DEFINE_GAMEPLAY_TAG(Item_Rarity_Legendary, "Item.Rarity.Legendary");
UE_DEFINE_GAMEPLAY_TAG(Item_Rarity_Relic, "Item.Rarity.Relic");

// Item Category
UE_DEFINE_GAMEPLAY_TAG(Item_Category, "Item.Category");
UE_DEFINE_GAMEPLAY_TAG(Item_Category_Weapon, "Item.Category.Weapon");
UE_DEFINE_GAMEPLAY_TAG(Item_Category_Material, "Item.Category.Material");
UE_DEFINE_GAMEPLAY_TAG(Item_Category_Consumable, "Item.Category.Consumable");
UE_DEFINE_GAMEPLAY_TAG(Item_Category_Clue, "Item.Category.Clue");
UE_DEFINE_GAMEPLAY_TAG(Item_Category_Skill, "Item.Category.Skill");

// Enemy
UE_DEFINE_GAMEPLAY_TAG(Item_EnemyWeapon_Sword, "Item.EnemyWeapon.Sword");
UE_DEFINE_GAMEPLAY_TAG(Item_EnemyWeapon_Hand, "Item.EnemyWeapon.Hand");
UE_DEFINE_GAMEPLAY_TAG(Item_EnemyWeapon_Bow, "Item.EnemyWeapon.Bow");

// Enemy Type
// 적 구분 태그
UE_DEFINE_GAMEPLAY_TAG(Enemy_Human_Test0, "Enemy.Type.Human.Test0");
UE_DEFINE_GAMEPLAY_TAG(Enemy_Human_Test1, "Enemy.Type.Human.Test1");
UE_DEFINE_GAMEPLAY_TAG(Enemy_Ship_Test0, "Enemy.Type.Ship.Test0");
UE_DEFINE_GAMEPLAY_TAG(Enemy_Ship_Test1, "Enemy.Type.Ship.Test1");
UE_DEFINE_GAMEPLAY_TAG(Enemy_Ship_Test2, "Enemy.Type.Ship.Test2");


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
UE_DEFINE_GAMEPLAY_TAG(Key_UI_Tab, "Key.UI.Tab");

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
UE_DEFINE_GAMEPLAY_TAG(Interaction_Dialogue, "Interaction.Dialogue");
