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
- `GetThreadSafeWeaponUpperBodyDirection()`

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
- `bForceSprintWeaponUpperBodyDirectionForward`

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
4. Feed `GetThreadSafeWeaponUpperBodyDirection()` into each weapon upper-body BlendSpace direction axis.
   - With `bForceSprintWeaponUpperBodyDirectionForward = true`, sprint uses direction `0` so a forward-only sprint animation works cleanly.
5. Feed `GetThreadSafeWeaponUpperBodyOverlayIndex()` into `Blend Poses by Int` to choose the active weapon BlendSpace.
6. Plug the normal locomotion pose into the weapon `Layered Blend Per Bone` `Base Pose`.
7. Plug the selected weapon upper-body BlendSpace pose into `Blend Poses 0`.
8. Send the weapon layered output into `Slot 'UpperBody'`.
9. Plug `GetThreadSafeWeaponUpperBodyAlpha()` into `Blend Weights 0`.
10. Keep the weapon overlay branch filter conservative. On the current player skeleton, prefer arm/shoulder filters first:
    - `clavicle_l` / depth `4`
    - `clavicle_r` / depth `4`
11. Only add a spine filter if the weapon pose needs torso involvement, and keep it shallow:
    - optional `spine_03` / depth `1`
12. Avoid deep `spine_02`, `spine_03`, `spine_04`, or `spine_05` filters for weapon locomotion overlays unless the source animations are verified to have clean translation keys. This skeleton has `neck_01`, `neck_02`, and `head` under `spine_05`, so deep spine filters can pull neck/head transforms into the blend and cause head rotation or neck stretching.
13. Continue into the existing aim offset / final upper-lower body blend / foot placement chain.

Looping weapon run and sprint animations are fine for this layer as long as they are in-place. If the source animations are full-body, the lower body will be ignored by the layered blend, so focus on whether the shoulders, arms, and hands look stable with the weapon.

Layered blend settings used for the weapon overlay:

- `Mesh Space Rotation Blend`: on
- `Root Space Rotation Blend`: off
- `Mesh Space Scale Blend`: off
- `Blend Root Motion Based On Root Bone`: off while debugging retarget/stretch issues

If the character's neck stretches only when `GetThreadSafeWeaponUpperBodyAlpha()` is connected, first test the weapon `Layered Blend Per Bone` with only `clavicle_l` and `clavicle_r` filters. If that fixes the pose, the problem is the spine/neck translation being pulled into the blend, not the aim offset or montage slot.

## Bow Draw Animation Sync

`GA_BowAimFire` already owns the bow draw gameplay flow:

1. Right click activates the bow aim/fire ability.
2. Left click hold starts drawing.
3. `GA_BowAimFire::UpdateDrawAlpha()` increases `UBowComponent::DrawAlpha` from `0.0` to `1.0`.
4. Left click release fires the arrow on the server if the draw alpha is high enough.
5. `UBowComponent::DrawAlpha` is replicated, so remote clients can see the same draw state.

The anim instance now exposes bow state to the ABP:

- `GetThreadSafeIsBowAiming()`
- `GetThreadSafeIsBowDrawing()`
- `GetThreadSafeIsBowFullyDrawn()`
- `GetThreadSafeIsBowReleasing()`
- `GetThreadSafeBowDrawAlpha()`
- `GetThreadSafeHasBowStringIKTarget()`
- `GetThreadSafeBowStringIKAlpha()`
- `GetThreadSafeBowStringIKTargetTransform()`

Use `GetThreadSafeBowDrawAlpha()` as the single sync value for all bow draw visuals:

- bow string deformation
- bow draw pose / 1D BlendSpace / Sequence Evaluator
- fire speed and damage scaling

Recommended socket setup on the bow skeletal mesh:

- `String_Rest_Socket`: where the right hand/string sits at draw alpha `0.0`
- `String_Draw_Socket`: where the right hand/string sits at draw alpha `1.0`

The character's authored Draw, Full Draw Aim Idle, and Release animations are the authority for `hand_r`. Do not drive the hand toward the bow string as the normal path; that reverses ownership and makes the hand visibly chase the bow.

`ABowItem` still resolves the two string sockets and exposes a blended component-space target for optional correction. `UMotionMatchingAnimInstance::bEnableBowStringHandIK` defaults to `false`, so the character animation remains untouched. Keep the existing FABRIK node wired after the final upper-body pose and before foot placement, but its alpha will be zero by default:

```text
Slot 'DefaultSlot'
 -> Local To Component
 -> FABRIK / Two Bone IK for hand_r
 -> Foot Placement
```

Use `GetThreadSafeBowStringIKTargetTransform()` as the IK effector transform and `GetThreadSafeBowStringIKAlpha()` as the IK alpha. Leave the new `Enable Bow String Hand IK` setting disabled until the grip socket, full-draw sequence, and aim offset are all correct. If enabled later, it runs only at full draw and is intended for a small residual correction, not for creating the draw pose.

### Bow string follows authored fingers

For the primary string alignment path, do not enable character FABRIK. Add a `BowStringGrip` socket to the character hand that pulls the string and position it at the finger pinch in the full-draw pose. `ABowItem::GetCharacterStringGripTargetTransform()` returns that socket in `BowMesh` component space. In the Bow animation blueprint, use the returned transform to drive the string-center bone (currently `Bone_end`) with a Transform Modify Bone or a Bow Control Rig. Blend that correction with `DrawAlpha`; the bow moves to the authored hand, while the character montage remains authoritative for the hand pose.

## Weapon Grip Alignment

Weapon attachment now supports socket-to-socket alignment. Set `EquipSocketName` to the character socket and `ItemGripSocketName` to the matching socket on the item mesh. The equipment component aligns the item grip socket to the character socket instead of attaching the item actor root directly.

For the bow entry, use:

```text
EquipSocketName: Bow
ItemGripSocketName: GripSocket
```

`ABowItem` hides its inherited static `ItemMesh`; its `BowMesh` skeletal component is the visible bow so its animation and string sockets remain the single visual source.

`ABaseItem::GetAttachmentReferenceComponent()` identifies the component that owns an item's grip socket. The default returns the actor root, while `ABowItem` overrides it to return `BowMesh`. This keeps socket-to-socket attachment deterministic as more weapon types are added.

During Draw, Full Draw, and Release, `bSuppressAimOffsetWhileBowFullyDrawn` defaults to true. This prevents the global aim offset from rotating an authored bow pose after it has been selected. Disable it only after introducing a bow-specific additive aim layer that is authored to work with those poses.

When draw reaches its maximum alpha, the anim instance treats `DrawAlpha == 1.0` as fully drawn immediately, then also reads `State.Bow.FullyDrawn`. The ability sets that state before blending out the Draw montage, so the Full Draw pose is already selected underneath the montage and the normal bow overlay cannot flash between them.

`GetThreadSafeShouldUseBowFullDrawPose()` additionally preloads the Full Draw pose at `FullDrawPosePreloadAlpha` (default `0.9`). Use this single boolean for the Full Draw `Blend Poses by Bool` active value; it already excludes the release state. This gives the upper-body graph one animation update to prepare the hold pose before the Draw montage fades out.

## Weapon Action Cycle Montage

The bow now supports a single `AimCycleMontage` from its `FWeaponAnimationEntry`. Configure this on the bow entry in the weapon animation DA:

```text
Aim Cycle Montage: AM_BowAimCycle
Aim Cycle Draw Section Name: Bow_Draw
Aim Cycle Hold Section Name: Bow_Hold
Aim Cycle Release Section Name: Bow_Release
Aim Cycle Play Rate: 1.0
Aim Cycle Blend Out Time: 0.1
```

`AM_BowAimCycle` must use the existing `DefaultGroup.UpperBody` slot. Its sections should flow as follows:

```text
Bow_Draw -> Bow_Hold -> Bow_Hold (loop)
Bow_Hold -> Bow_Release (when left click is released)
Bow_Release -> End
```

The bow ability starts this montage at `Bow_Draw`, jumps to `Bow_Hold` on full draw, and jumps to `Bow_Release` on left-click release. It no longer stops a Draw montage and exposes the base bow overlay between the draw and hold poses. If `AimCycleMontage` is left unset, the old separate Draw/Release montage path remains available while assets are being migrated.

For exact bow-string synchronization, author a float curve named `Weapon.DrawAlpha` on the character sequences in the Aim Cycle montage, then enable `Use Aim Cycle Draw Alpha Curve` on `GA_BowAimShoot`:

```text
Bow_Draw: 0 at rest, then 0 -> 1 while the character pulls the string
Bow_Hold: 1 for the whole loop
Bow_Release: 1 -> 0 while the string returns
```

The curve is copied to replicated `UBowComponent::DrawAlpha`, which drives the bow skeletal mesh animation for remote clients too. Leave the option disabled until the curve exists; the legacy timing values remain the fallback.

`GameplayAbility.Weapon.AimCycle` is now the common cancellation tag for weapon aiming. `UPlayerEquipmentComponent` cancels active abilities with this tag before a weapon switch begins. Dodge and hit-reaction abilities should also cancel this tag in their ability settings (or call `CancelAbilities` with it) once their gameplay tags are finalized.

The global aim offset remains suppressed during Bow Draw/Hold/Release so it cannot overwrite authored action poses. For the Hold section only, use `GetThreadSafeBowHoldAimOffsetAlpha()` in the bow upper-body layer. It returns `BowHoldAimOffsetAlpha` (default `1.0`) only while the bow is fully drawn and not releasing. Apply the existing aim-offset additive pose through a second `Layered Blend Per Bone` rooted at `spine_03`; enable Mesh Space Rotation Blend and leave Mesh Space Scale Blend disabled. This rotates the chest and arms with the camera while preserving the draw and release animations.

Sprint is blocked while the bow is being drawn by using the existing `State.Attacking` gameplay tag. Draw movement is still allowed, but sprint should not start or continue until the left click draw is released.

Bow fire is intentionally gated behind full draw:

1. Left click starts a shot if the bow is not already drawing, fully drawn, or releasing.
2. If `DrawMontage` is assigned, the ability plays that montage on draw start.
3. `DrawAlpha` advances from `0.0` to `1.0`.
4. When `DrawAlpha` reaches `FullDrawAlphaToRelease`, the ability stops the draw montage, enters fully drawn hold state, and keeps `DrawAlpha` at `1.0`.
5. The ABP can blend into a full-draw aim idle pose while `State.Bow.FullyDrawn` is active.
6. Releasing left click from fully drawn state enters release state.
7. If `ReleaseMontage` is assigned, the ability plays that montage.
8. Put `AN_SendGameplayEvent` on the frame where the hand releases the string, and set its tag to `Event.Montage.FireArrow`.
9. `GA_BowAimFire` fires the arrow only from that event while the shot is fully drawn and release is in progress.
10. Releasing left click before full draw cancels the draw and does not fire.
11. Extra left-click presses are ignored while drawing, fully drawn, or releasing.

The bow ability also publishes these GAS state tags:

- `State.Bow.Drawing`
- `State.Bow.FullyDrawn`
- `State.Bow.Releasing`

These tags are useful for animation conditions, sprint blocking, debugging, and future UI feedback. The existing `State.Attacking` tag remains the broad combat/action gate.

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
