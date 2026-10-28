"""Read-only inspection of the active swim blend spaces and their hand poses."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()).resolve() / 'AudioIntegration/SwimContacts'
out.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,
                                               include_soft_package_references=True)
result = {'abp_dependencies': [str(x) for x in registry.get_dependencies('/Game/Anim_Logic/ABP_Swim', options)],
          'blends': [], 'sequences': []}
clips = {}
for package in ['/Game/Anim_Logic/BSs/BS_Swim_Surface', '/Game/Anim_Logic/BSs/BS_Swim_Underwater']:
    blend = unreal.load_asset(package)
    row = {'package': package, 'notify_mode': str(blend.get_editor_property('notify_trigger_mode')), 'samples': []}
    for sample in blend.get_editor_property('sample_data'):
        sequence = sample.get_editor_property('animation')
        if not sequence:
            continue
        row['samples'].append({'animation': sequence.get_path_name(), 'coordinate': str(sample.get_editor_property('sample_value')),
                               'rate': sample.get_editor_property('rate_scale')})
        clips[sequence.get_path_name()] = sequence
    result['blends'].append(row)
for path, sequence in clips.items():
    length = sequence.get_play_length()
    row = {'asset': path, 'length': length, 'notifies': list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(sequence)), 'poses': []}
    for i in range(120):
        time = i * length / 120
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, time, unreal.AnimPoseEvaluationOptions())
        values = {'time': time}
        for bone in ['hand_l', 'hand_r', 'pelvis', 'head']:
            v = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
            values[bone] = [v.x, v.y, v.z]
        row['poses'].append(values)
    result['sequences'].append(row)
(out / 'inspection.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('SW_SWIM_CONTACT_INSPECTION_COMPLETE clips=' + str(len(clips)))
