# Equipment And Weapon Animation Notes

Date: 2026-07-06

## Goal

Move equipment responsibilities out of `ABasePlayer` step by step, keep the current inventory surface stable, and expose enough thread-safe animation data for the Animation Blueprint to react to equipped weapons.

## Added Files

- `Source/ClassFeature/Public/Equipment/PlayerEquipmentComponent.h`
- `Source/ClassFeature/Private/Equipment/PlayerEquipmentComponent.cpp`

## Equipment Component

`UPlayerEquipmentComponent` now owns the equipment transition flow:

- Replicated `EEquipmentState`
- 1/2/3 quick-slot equip request handling
- Server-authoritative equip decisions
- Weapon animation data asset lookup
- Equip montage playback through multicast
- `Equipment Attach Item` anim notify handling
- Equipped item ability grant and removal
- Equipped item socket attach and stored socket attach

`ABasePlayer` keeps its existing public API and delegates equipment calls to the component:

- `EquipItemFromSlot()`
- `UseEquippedItem()`
- `GetEquipmentState()`
- `IsEquipmentTransitioning()`
- `HandleEquipmentAttachNotify()`
- `OnRep_EquippedItem()`

`EquippedItem` and `ItemSlots` intentionally remain in `ABasePlayer` for this pass because existing UI, Blueprint, pickup, and replication code still depend on them directly.

## Per-Weapon Animation Data

`UWeaponAnimationDataAsset` can now be used as one animation/combat profile per weapon.

Recommended asset layout:

- `DA_BowWeaponAnimation`
- `DA_SwordWeaponAnimation`
- `DA_SpearWeaponAnimation`

Each weapon data asset can define:

- `EquipMontage`
- `UnequipMontage`
- `CombatIntroMontage`
- `CombatOutroMontage`
- `ReloadMontage`
- montage play rates
- equip/stored socket names
- upper-body overlay tag
- upper-body overlay index
- input-tag-to-GA bindings

`UPlayerEquipmentComponent` has a tag mapping so each item tag can resolve to its own weapon animation data asset. The old single `WeaponAnimationData` property remains as a fallback/default profile.

Weapon equip is treated as entering combat. When a weapon is finalized as equipped, the player enters combat mode immediately. `ABasePlayer::CombatIntroMontage` remains as a fallback for non-weapon or legacy setup, but equipped weapon DA combat intro takes priority when `SetCombatMode(true)` is called.

## Weapon Upper Body Overlay Data

`UMotionMatchingAnimInstance` now exposes thread-safe weapon upper-body data for ABP use:

- `GetThreadSafeHasWeaponEquipped()`
- `GetThreadSafeEquippedWeaponTag()`
- `GetThreadSafeWeaponUpperBodyOverlayTag()`
- `GetThreadSafeWeaponUpperBodyOverlayIndex()`
- `GetThreadSafeHasBowEquipped()`
- `GetThreadSafeShouldOverrideWeaponUpperBody()`
- `GetThreadSafeWeaponUpperBodyMode()`
- `GetThreadSafeWeaponUpperBodyState()`
- `GetThreadSafeWeaponUpperBodyAlpha()`
- `GetThreadSafeWeaponUpperBodySpeed()`

The anim instance reads the equipped item through `UPlayerEquipmentComponent`. The component resolves the current `FWeaponAnimationEntry` from `UWeaponAnimationDataAsset`, then exposes the weapon's upper-body overlay tag when `bUseUpperBodyOverlay` is enabled.

When a weapon overlay is enabled and the locomotion state is grounded (`Idle`, `Start`, `Locomotion`, or `Stop`), the ABP can override only the upper body while Motion Matching continues to drive the base locomotion.

Common overlay state selection:

- `None`: no weapon overlay
- `Idle`: weapon equipped and not moving
- `Run`: weapon equipped and moving
- `Sprint`: weapon equipped and sprinting

Weapon identity is kept as a gameplay tag:

- `GetThreadSafeEquippedWeaponTag()`: the actual equipped item tag, such as `Item.Weapon.Bow`
- `GetThreadSafeWeaponUpperBodyOverlayTag()`: the overlay profile tag from the weapon animation data asset; if empty, it falls back to the equipped item tag
- `GetThreadSafeWeaponUpperBodyOverlayIndex()`: the ABP selection index for `Blend Poses by Int`; use this to choose Bow/Sword/Spear upper-body BlendSpace players

Tunable defaults in `UMotionMatchingAnimInstance`:

- `bEnableWeaponUpperBodyOverlay`
- `WeaponUpperBodyMovingSpeedThreshold`

The old bow-specific getters remain as compatibility helpers while the ABP is moved to the generic weapon overlay functions.

## ABP Setup For Upper/Lower Body Split

Recommended graph position:

1. Use cached pose `Locomotion` as the base.
2. Place one upper-body BlendSpace Player per weapon in the ABP.
   - `0`: no weapon/default pose
   - `1`: `BS_Bow_UpperBody`
   - `2`: `BS_Sword_UpperBody`
   - `3`: `BS_Spear_UpperBody`
3. Feed `GetThreadSafeWeaponUpperBodySpeed()` into each weapon upper-body BlendSpace speed axis.
4. Feed `GetThreadSafeWeaponUpperBodyOverlayIndex()` into `Blend Poses by Int` to choose the active weapon BlendSpace.
5. Plug the normal locomotion pose into the weapon `Layered Blend Per Bone` `Base Pose`.
6. Plug the selected weapon upper-body BlendSpace pose into `Blend Poses 0`.
7. Send the weapon layered output into `Slot 'UpperBody'`.
8. Plug `GetThreadSafeWeaponUpperBodyAlpha()` into `Blend Weights 0`.
9. Set the branch filter to `spine_01` or `spine_02` depending on the skeleton, with enough depth to include chest, shoulders, arms, neck, and head if needed.
10. Continue into the existing aim offset / final upper-lower body blend / foot placement chain.

Looping weapon run and sprint animations are fine for this layer as long as they are in-place. If the source animations are full-body, the lower body will be ignored by the layered blend, so focus on whether the spine, shoulders, and hands look stable with the weapon.

## Current Flow

1. Quick-slot input calls `ABasePlayer::EquipItemFromSlot()`.
2. `ABasePlayer` delegates to `UPlayerEquipmentComponent`.
3. Clients request equip through the component Server RPC.
4. The server validates the slot item and enters `Equipping`.
5. The component resolves the weapon animation entry from the data asset.
6. The equip montage is multicast to all clients.
7. The `Equipment Attach Item` notify attaches the item to the equip socket.
8. The server updates `EquippedItem`, item state, and equipped item GAS ability.
9. Replicated equipped item changes keep client-side item attachment in sync.
10. The anim instance reads the equipped item tag and exposes generic weapon overlay values to the ABP.

## Next Refactor Candidates

1. `UPlayerInventorySlotComponent`
   - `ItemSlots`
   - `TryPutItemInSlot`
   - `RemoveItemFromSlot`
   - `OnItemSlotsChanged`

2. `UPlayerInteractionComponent`
   - F-key interaction
   - `PerformInteractTrace`
   - Interaction UI scan
   - `Interaction_PickUp`, `Interaction_ShipBoard` event handling

3. `UPlayerAbilityInputComponent`
   - `OnAbilityInputPressed`
   - `OnMouseInputPressed`
   - input tag to GAS input routing

## Verification

Built successfully:

```text
ArtisticSW2026Editor Win64 Development
Result: Succeeded
```
