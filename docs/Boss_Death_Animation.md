# Boss death animation

`AShipBossEnemy` dies at its current world pose, attaches to `HostShip->GetShipDeckMesh()` using the corresponding relative transform, plays its configured death montage, and retains the existing `CorpseLifetimeAfterDeathFinished` destruction delay (currently 5 seconds). The occupied/destination deck points are released immediately; neither point is used to relocate the corpse.

The server destroys the boss weapon at death start using `DestroyCurrentWeapon()`. AI, dash damage, capsule collision, movement and root-motion movement stop. The skeletal animation continues, including offscreen. Native attachment replication carries the authoritative parent and local transform; clients do not independently choose a death position. Late Vanish cleanup and visibility/host RepNotifies cannot restore walking or collision, or hide the corpse. Host destruction still destroys the boss immediately.

## Assets and authoring

- Existing `BPGA_BossDeath` uses the artist-authored `/Game/Fab/Samurai/Animations/Montages/AM_Samurai_Death` (approximately 2.767 seconds).
- The montage uses `DefaultSlot`, one non-looping section, no death-ragdoll notify, and **Enable Auto Blend Out = false**. It holds the final pose until destruction. Keep **Stop Death Montage When Ability Ends = false** on the death GA.
- A different boss can select a different death GA/montage through the existing `DeathAbilityClass`. Its montage must use a compatible skeleton/AnimBP slot and the same single-pass, non-looping, no-auto-blend-out contract.
- `UBaseDeathGameplayAbility` completes non-blending montages using their playback duration, including play rate, asset rate scale, and start-section offset. It does not wait for the completion delegate that a held montage never emits. Auto-blending/player montages retain their callback and fallback behavior.
- No pool lifecycle was added. With the current configuration, total visible death time is approximately 2.767 + 5 seconds.

`Scripts/author_boss_death.py` reproducibly configures the artist-authored montage and death GA through Unreal Editor Python. It verifies skeleton/slot compatibility and preserves the montage's animation content, sections, and authored blend profile. Rerunning it reapplies the final-pose and GA defaults.

## Verification

Automation filter: `ArtisticSW.Enemy.BossDeath`.

The tests exercise the actual death pipeline: off-waypoint death while hidden, immediate weapon destruction, unchanged initial position, translation/yaw/pitch/roll following, released point occupancy, late replication/relocation callbacks, non-ragdoll policy, montage playback and final-pose hold, timed destruction, interrupted montage retirement, and host destruction. The existing `ArtisticSW.Enemy.DeathRagdoll` tests cover regular-enemy regression.

These are headless tests. Visual presentation and multiplayer latency should additionally be checked in PIE with a moving ship: kill the boss while standing, dashing, and vanishing; verify the same local position on server/client, disappearance of its weapon, full death animation, held final pose, and five-second retirement delay. Do not infer a completed multi-client visual check from the headless tests.
