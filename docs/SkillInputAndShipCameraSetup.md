# Skill and ship Enhanced Input setup

The C++ side no longer binds skill keys directly. Create or assign the following
Input Action assets in the relevant Blueprint defaults, then add them to the
mapping context that is active for that possession state.

| Possession state | Input Action | Value type | Mapping | C++ property |
| --- | --- | --- | --- | --- |
| Player/on foot | `IA_GravityVortex` | Digital | `3` | `GravityVortexSkillAction` |
| Cannon | existing `IA_CannonWaterBomb` | Digital | `4` | `CannonWaterBombToggleAction` |
| Ship | `IA_ShipBombardment` | Digital | `5` | `ShipBombardmentToggleAction` |
| Ship | `IA_ShipBombardmentConfirm` | Digital | Left Mouse Button | `ShipBombardmentConfirmAction` |
| Ship | `IA_ShipBombardmentCancel` | Digital | Right Mouse Button and Escape | `ShipBombardmentCancelAction` |
| Ship | `IA_ShipZoom` | Axis1D | Mouse Wheel Axis | `ShipZoomAction` |

Recommended mapping contexts:

- Put `IA_GravityVortex` in the on-foot `DefaultIMC` used by `ABasePlayer`.
- `Content/New/Cannon/CannonIMC` already contains `IA_CannonWaterBomb`; assign
  that existing IA to `CannonWaterBombToggleAction` on `BP_Cannon`.
- Put the four new ship actions in
  `Content/New/Ship/Input/IMC_Ship`, then assign them on the ship Blueprint.

For `IA_ShipZoom`, mouse wheel up should produce `+1` and wheel down `-1`.
The code subtracts positive input from the spring-arm length, so wheel up zooms
in. Zoom limits and step size are exposed under `Ship | Camera | Zoom`.

## Bombardment decal preview

`ABombardmentPreview` now contains both the old static mesh component and a
flat decal component. Existing previews remain on the mesh path until the
Blueprint child enables `bUseDecalPreview`.

1. Create a material with **Material Domain = Deferred Decal**.
2. Use a circular texture/mask for opacity and set the material's decal blend
   settings to match the desired color/normal response.
3. In the Bombardment Preview Blueprint, enable `bUseDecalPreview`.
4. Assign the decal material to `ValidPreviewDecalMaterial` and optionally a
   second material to `InvalidPreviewDecalMaterial`.
5. Adjust `DecalProjectionDepth` only if uneven terrain falls outside the
   projection volume.

The decal component uses the skill radius for its Y/Z extents and projects
downward. It is visually two-dimensional; `DecalProjectionDepth` is only the
invisible projection volume.
