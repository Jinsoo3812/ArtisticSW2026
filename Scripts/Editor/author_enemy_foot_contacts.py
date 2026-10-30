"""Author enemy-only contacts from locomotion poses. Run outside PIE; no player clip edits.

Backs up every changed package under Saved/AudioIntegration/EnemyContacts before
saving. The original animation poses, curves and existing notifies are preserved.
"""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Saved/AudioIntegration/EnemyContacts"
OUT.mkdir(parents=True, exist_ok=True)
DIRECTIONS = ["Bwd", "Bwd_Left", "Bwd_Right", "Fwd", "Fwd_Left", "Fwd_Right", "Left", "Right"]


def contact_plan(sequence):
    length = sequence.get_play_length()
    options = unreal.AnimPoseEvaluationOptions()
    samples = []
    for i in range(180):
        time = length * i / 180
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, time, options)
        samples.append({"time": time, **{bone: unreal.AnimPoseExtensions.get_bone_pose(
            pose, bone, unreal.AnimPoseSpaces.WORLD).translation.z for bone in ["foot_l", "foot_r"]}})
    contacts = []
    for bone, left in [("foot_l", True), ("foot_r", False)]:
        heights = [s[bone] for s in samples]
        low, high = min(heights), max(heights)
        assert high - low > 8, (sequence.get_path_name(), bone, "No distinct swing")
        plant = low + min(3.5, (high - low) * 0.16)
        rearm = low + (high - low) * 0.55
        armed = False
        # Two periods resolve a planted foot crossing the loop seam without
        # inventing an event at t=0 or adding two events for one planted interval.
        for lap in range(2):
            for sample in samples:
                if sample[bone] >= rearm:
                    armed = True
                if armed and sample[bone] <= plant:
                    if lap == 1:
                        contacts.append({"time": sample["time"], "left": left, "height": sample[bone]})
                    armed = False
    contacts.sort(key=lambda c: c["time"])
    assert len(contacts) >= 2 and all(contacts[i]["left"] != contacts[(i + 1) % len(contacts)]["left"] for i in range(len(contacts))), sequence.get_path_name()
    return contacts


def author():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert not editor or not editor.get_game_world(), "End PIE before authoring"
    reports = []
    # Deliberately scoped to the existing enemy blend spaces' 16 locomotion clips.
    for gait in ["Walk", "Jog"]:
        for direction in DIRECTIONS:
            package = f"/Game/Characters/Mannequins/Anims/Unarmed/{gait}/MF_Unarmed_{gait}_{direction}"
            sequence = unreal.load_asset(package)
            assert isinstance(sequence, unreal.AnimSequence), package
            before = list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(sequence))
            path = ROOT / "Content" / (package.removeprefix("/Game/") + ".uasset")
            backup = OUT / "Backups" / path.relative_to(ROOT / "Content")
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(path, backup)
            contacts = contact_plan(sequence)
            for contact in contacts:
                assert unreal.SWFoleyMigrationLibrary.add_enemy_contact(sequence, unreal.SWAnimNotify_EnemyFootContact,
                    contact["time"], contact["left"]), package
            after = list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(sequence))
            assert all(event in after for event in before), "An existing notify changed: " + package
            assert unreal.EditorAssetLibrary.save_loaded_asset(sequence, True), package
            reports.append({"package": package, "backup_sha256": hashlib.sha256(backup.read_bytes()).hexdigest(),
                "contacts": contacts, "before": before, "after": after})
    # Reject multi-source blend notifications rather than adding a second clock.
    for package in ["/Game/Fab/Stylized_Warrior/BS_Warrior", "/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run"]:
        blend = unreal.load_asset(package)
        assert blend.get_editor_property("notify_trigger_mode") == unreal.NotifyTriggerMode.HIGHEST_WEIGHTED_ANIMATION, package
    (OUT / "authored.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
    unreal.log("SW_ENEMY_CONTACTS_AUTHORED clips=" + str(len(reports)) + " markers=" + str(sum(len(r["contacts"]) for r in reports)))


def configure_enemy_gaits():
    """Keep idle from winning the notify clock at ordinary 140cm/s walking."""
    for package in ["/Game/Fab/Stylized_Warrior/BS_Warrior", "/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run"]:
        blend = unreal.load_asset(package)
        path = ROOT / "Content" / (package.removeprefix("/Game/") + ".uasset")
        backup = OUT / "Backups" / path.relative_to(ROOT / "Content")
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(path, backup)
        samples = list(blend.get_editor_property("sample_data"))
        if package.endswith("BS_Warrior"):
            # Its original grid had only idle=0 and jog=500, so idle suppressed
            # every foot notify at ordinary walking speed. Add real walk poses.
            for sample in list(samples):
                value = sample.get_editor_property("sample_value")
                if value.y < 499:
                    continue
                jog = sample.get_editor_property("animation")
                walk = unreal.load_asset(jog.get_path_name().split(".")[0].replace("/Jog/", "/Walk/").replace("_Jog_", "_Walk_"))
                assert walk, jog.get_path_name()
                if any(abs(s.get_editor_property("sample_value").x - value.x) < 0.01 and abs(s.get_editor_property("sample_value").y - 150) < 0.01 for s in samples):
                    continue
                added = unreal.BlendSample()
                added.set_editor_property("animation", walk)
                added.set_editor_property("sample_value", unreal.Vector(value.x, 150, 0))
                samples.append(added)
        else:
            for sample in samples:
                animation = sample.get_editor_property("animation")
                if animation and "/Walk/" in animation.get_path_name():
                    value = sample.get_editor_property("sample_value")
                    sample.set_editor_property("sample_value", unreal.Vector(value.x, 150, value.z))
        blend.set_editor_property("sample_data", samples)
        unreal.SWFoleyMigrationLibrary.rebuild_contact_blend_space(blend)
        assert unreal.EditorAssetLibrary.save_loaded_asset(blend, True), package


if __name__ == "__main__":
    author()
    configure_enemy_gaits()
