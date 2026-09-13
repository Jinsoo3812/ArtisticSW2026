# Progression balance (initial draft)

The runtime source of truth is `/Game/Blueprints/Item/Data/DA_ProgressionBalance`.
The 16 chest definitions and DTs are in `Blueprints/Item/Data/ChestDefinition` and
`LootTable`; the eight random groups are in `RandomGroup`. Existing `Land`, `Ocean`,
and `Ship` asset names were retained for placed-map references. `Land` means an
unguarded island chest; `Ship` means a guarded deck chest. The fourth pool is
`IslandGuarded`. A sunk ship uses its manually assigned deck definition with the
central 0.5 expected-roll multiplier, and always spawns the chest independently.

`DA_ProgressionBalance` controls the live loot pools, roll counts, storage sizes,
random-group activation counts, material costs for weapon/consumable recipes and
ship upgrade nodes, and the two-clear affordability audit. The DT rows and
per-node activation costs are readable snapshots/fallbacks. Run the editor test
`ArtisticSW.Chest.Authoring.GenerateRewardAssets` to refresh those snapshots;
gameplay reads the central DA directly even before a snapshot refresh.

Initial per-zone plan (editable independently): three ship squads with three
ships each, three island guard squads, five active island random points out of
ten candidates, and six active ocean random points out of twelve candidates.
The active random-point counts are enforced by `GlobalLootSpawnManager` when each
zone/type uses its own random group. The candidate-point counts and guarded squad
counts are planning/audit targets: level authors must place the corresponding
points and squads. Do not reuse the same random group across different zones.

The two-clear audit assumes one next-tier weapon, three next-tier ship nodes,
and three next-tier consumables, with all deck chests boarded. Sinking ships
instead lowers the expected material return. Additional recipe ingredients and
rare loot entries can be added once to `GlobalLootEntries` to affect every chest
without changing the roll or crafting pipeline. Existing fifth-tier weapon recipes are disabled by
the four-tier runtime bridge; a deliberate recipe binding can opt one back in.

The project currently has only three authored consumable products in each heal
and buff line. The fourth-tier material template and extension path exist, but
a distinct fourth-tier consumable item/effect has not been invented. Likewise,
the status UI has an experience display, but no player XP award/progression
implementation was found, so the two-clear XP target is not enforced yet.
The existing `QuestItem` row in `DT_CraftingRecipes` is malformed and emits
validation errors during game-world initialization; it is outside this balance
conversion and was left untouched.
