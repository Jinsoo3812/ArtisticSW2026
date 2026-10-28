"""Add surface hand-catch markers to the active eight swim cycles, preserving poses."""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/AudioIntegration/SwimContacts'
inspection = json.loads((out / 'inspection.json').read_text(encoding='utf-8'))
poses = {row['asset']: row for row in inspection['sequences']}
blend = unreal.load_asset('/Game/Anim_Logic/BSs/BS_Swim_Surface')
assert blend.get_editor_property('notify_trigger_mode') == unreal.NotifyTriggerMode.HIGHEST_WEIGHTED_ANIMATION
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor or not editor.get_game_world(), 'End PIE before authoring'
clips = {sample.get_editor_property('animation').get_path_name(): sample.get_editor_property('animation')
         for sample in blend.get_editor_property('sample_data') if sample.get_editor_property('animation')}
report = []

def pose_signature(clip):
    values = []
    for fraction in [0., .25, .5, .75]:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, fraction * clip.get_play_length(), unreal.AnimPoseEvaluationOptions())
        for bone in ['hand_l', 'hand_r', 'pelvis', 'head']:
            transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
            v = transform.translation
            values.append([v.x, v.y, v.z])
    return values

for path, clip in clips.items():
    if clip.get_name() == 'anim_swim_idle_A':
        continue
    row = poses[path]
    assert abs(row['length'] - clip.get_play_length()) < .001
    package = clip.get_path_name().split('.')[0]
    file = root / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    backup = out / 'Backups' / file.relative_to(root / 'Content')
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(file, backup)
    before = list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(clip))
    original_pose = pose_signature(clip)
    contacts = []
    for bone, left in [('hand_l', True), ('hand_r', False)]:
        # Freestyle catches start at maximal forward reach, before the pull.
        # Backstroke catches behind the head; lateral sculling uses maximal reach.
        if clip.get_name().startswith('anim_swim_forward_E'):
            best = max(row['poses'], key=lambda p: p[bone][1])
            rule = 'maximum_forward_reach'
        elif clip.get_name().startswith('anim_swim_backward_C02'):
            best = min(row['poses'], key=lambda p: p[bone][1])
            rule = 'maximum_backward_reach'
        else:
            best = max(row['poses'], key=lambda p: sum((p[bone][i] - p['pelvis'][i]) ** 2 for i in [0, 1]))
            rule = 'maximum_lateral_reach'
        time = best['time']
        assert unreal.SWFoleyMigrationLibrary.add_swim_contact(clip, time, left), path
        contacts.append({'time': time, 'left': left, 'hand_position': best[bone], 'rule': rule})
    after = list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(clip))
    assert all(event in after for event in before), 'An existing notify changed: ' + path
    assert pose_signature(clip) == original_pose, 'Animation pose changed: ' + path
    assert unreal.EditorAssetLibrary.save_loaded_asset(clip, False), path
    report.append({'package': package, 'contacts': contacts, 'existing_notifies': before, 'authored_notifies': after,
                   'original_sha256': hashlib.sha256(backup.read_bytes()).hexdigest(),
                   'saved_sha256': hashlib.sha256(file.read_bytes()).hexdigest(), 'pose_signature_preserved': True})
assert len(report) == 8
(out / 'authored.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('SW_SWIM_CONTACTS_AUTHORED clips=8 markers=16')
