# Playtest fixes, 2026-10-31

## Implementation Log & Checklist

- Campaign Yi Sun-sin NPC: assigned the authored NPC sash mesh, animation class, and matching component transform.
- Repair materials: imported four material assets from origin/CHAE_Upload. Existing SM_Woodpice geometry is reused in four separate material variants; registered WoodenPlank, GoodWoodenPlank, IronPlate, and GoodIronPlate in DA_ItemData.
- Player default maximum/current health: 1000, scoped to BasePlayerState so enemy defaults remain independent.
- Player ship DT health tiers: 3000, 4500, 6000, 7500, 9000. Runtime fallback: 3000.
- Session grants player and player ships 1,000,000 health once per life, with no invulnerability tag and no per-tick healing. Turning Session off restores ordinary maximum health; independent roll protection is preserved.
- Free crafting bypasses skill-item progression rejection. SkillFree permits skill eligibility without writing permanent unlock progress.
- F prioritizes the selected nearest helm, anchor, or crafting interaction over selected consumables.
- Waiting/game-over camera attaches to the corpse mesh; automatic teammate observation is disabled.
- Asset commandlet completed successfully. C++ build succeeded before integration; final merged build/tests are tracked below.

## fix-lss integration policy

User approved accepting compatible functionality from both branches. Full branch integration includes cannon operator animation, manual camera zoom, gameplay lifecycle/tag ownership, crafting cost aggregation, and swimming fixes.

- Cannon.cpp include conflict: retain development/weapon-input headers and operator profile/collision/skeletal-mesh headers.
- CraftingComponent.cpp: use aggregated BuildCosts while preserving CraftingFree checks and free-crafting detail overrides.
- GA_GravityVortexThrow.h: retain incoming preview fields and owned-tag helper.
- Preserve this playtest's input priority, corpse camera, skill crafting, health, and authored asset changes.
- No remote push requested.

## Final integration validation

- Integrated origin/fix-lss e08604dc into KKH/ShipAI with all four conflict regions resolved by retaining compatible logic from both branches.
- Final Unreal Editor Development build succeeded.
- Reopened assets in a new commandlet process: all four repair mesh references, Yi Sun-sin mesh/animation, and five ship health tiers verified.
- Legacy player saves with maximum health 100 migrate to maximum 1000 while preserving health ratio.
- Updated the existing corpse-camera test for attachment/following and the ship-tier test for the current DT path and all five tiers.
- Visual PIE and multiplayer playthrough were not performed in this run.

- Final regression run: 14 passed, 0 failed, 0 not run (5 passing tests emitted warnings).
