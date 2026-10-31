"""Run with UE 5.8 on the isolated inspector, never the user's original project."""
import json
from pathlib import Path
import unreal
path='/Game/Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData/PSS_SM_CMC_Idles'
schema=unreal.load_asset(path);assert schema
def scalar(value):return value.value if hasattr(value,'value') else value
properties=['sample_rate','data_preprocessor','number_of_permutations','permutations_sample_rate','permutations_time_offset','add_data_padding','inject_additional_debug_channels','draw_inject_additional_debug_channels']
result={'package':path,'properties':{p:scalar(schema.get_editor_property(p)) for p in properties},
 'skeletons':[s.export_text() for s in schema.get_editor_property('skeletons')],'channels':[]}
for channel in schema.get_editor_property('channels'):
    assert channel.get_class().get_name()=='PoseSearchFeatureChannel_Pose'
    result['channels'].append({'class':channel.get_class().get_name(),
      'properties':{p:scalar(channel.get_editor_property(p)) for p in ['weight','input_query_pose','use_character_space_velocities','permutation_time_type']},
      'sampled_bones':[b.export_text() for b in channel.get_editor_property('sampled_bones')]})
dest=Path(unreal.Paths.project_dir()).resolve().parent/'original_idle_schema.json'
dest.write_text(json.dumps(result,indent=2),encoding='utf8')
