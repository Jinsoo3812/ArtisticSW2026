# Applied progression balance baseline

The user approved these CSV values and they were applied to the existing item definitions, crafting recipe table, progression balance asset, and ship upgrade tree. The CSVs remain human-readable balance references, not directly importable Unreal DataTable CSVs; `Ingredients` uses `Tag:Quantity;Tag:Quantity` notation. The existing icons, abilities, spawn classes, ship node links, UI placement, and ship stat table were preserved. Progression weapon recipe-item gates (Epic/Legendary) were cleared for this baseline; the generic recipe-item feature remains in code for later special recipes.

## 1. Definition data

Use the existing `Content/Blueprints/Item/DA_ItemData` (`UItemData`), not a duplicate catalog. For each item tag in [item-definitions.csv](item-definitions.csv), set `ItemDefinitions[tag].ProgressionKind` and `ProgressionTier`. The weapon name suffix `1` is tier 0, suffix `5` tier 4. Materials are explicitly tiered instead of inferred from their names. Track-specific special materials may be used at any tier; `UniversalSpecialMaterial` works for every track. Other existing non-progression items stay `None`. This DA remains the source for item icons, abilities, and spawn classes too.

## 2. Crafting recipes

Use the existing `Content/Blueprints/Item/DT_CraftingRecipes` with row struct `FCraftingRecipeRow`. Enter each result tag and `ResultQuantity`, then expand `Ingredients` into item-tag/quantity pairs from [crafting-recipes.csv](crafting-recipes.csv). Set `bEnabled=true` on progression recipes, particularly the suffix-5 weapons previously disabled by the old four-zone implementation. Each result item must have only one enabled progression recipe. Weapon/consumable tier is read from the result item's definition, not from the legacy recipe tier fields. The runtime rejects materials of the wrong kind or outside the current/previous tier. The editor's recipe-ingredient picker reads `UItemData::GetCraftingMaterialOptions(ResultItemTag)` and lists only compatible materials; outside a crafting recipe, item stacks retain the normal tag picker.

## 3. Ship nodes

In the existing `Content/Blueprints/Item/Data/ShipUpgrade/DA_ShipUpgradeTree`, the 16 existing nodes now use the [ship-upgrade-node-costs.csv](ship-upgrade-node-costs.csv) starting pattern. Different nodes at one tier may have different costs later. The runtime uses these node costs directly. Each tier's average node cost feeds the chest calculation. The fourth track level and its `PlayerShip_4_IV` stat row already existed; no new ship stat row was necessary.

## 4. Progression

In the existing `Content/Blueprints/Item/Data/DA_ProgressionBalance`, use only the new `ZoneTargets` rows from [progression-targets.csv](progression-targets.csv): exactly Mid1, Mid2, Mid3, Final. `FullClears`, `WeaponCrafts`, `ConsumableCrafts`, and `ShipUpgrades` are the four editable values per zone. Old `ZonePlans`, `ChestPools`, and `ShipUpgradeCosts` are hidden compatibility fields and are ignored by the new chest manager calculation.

## 5. Level wiring

Place one `AGlobalLootSpawnManager` and assign its `ShipUpgradeTree` to `DA_ShipUpgradeTree`. Each `AChestSpawnPoint` (including ship child-actor points) sets `ProgressionZone` and `ProgressionKind`; no loot DA/DT or ChestDefinition is needed on the point. `ChestClassOverride` may reference the chest BP; otherwise the native `AStorageChest` is used. Guard and boss configuration stays on the point. Enemy ships forward their chest settings and automatically register crew as guards. For random chests, `URandomChestGroup` is used **only** to choose how many candidate points activate; its ChestDefinition is ignored. The manager finalizes loot on the next tick after BeginPlay and counts spawned point-owned chests, not candidate points and not sunk chests. On ship death, a buoyant chest spawns independently using the deck chest class; its material chances are the cached zone chances multiplied by `SunkChestExpectedValueRatio`. If ships/points spawn later, disable automatic initialization and call `InitializeLevelLoot` followed by `RebalanceSpawnedChests` after level generation. Finalization is one-shot to prevent loot rerolls.

For each material tag: average ingredient quantity across all enabled tier-N weapon recipes × `WeaponCrafts`, plus the equivalent consumable and ship-node terms. If there are `K` active chests and the zone target is `M` full clears, each chest has expected quantity `total / (K×M)`. Stack quantity is `max(1, ceil(expected / 0.85))`; drop probability is `expected / stack quantity`. Every active chest in the same zone uses the same distribution regardless of chest kind. This is expectation-based, not guaranteed.
