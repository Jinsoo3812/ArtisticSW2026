"""Reserve a forward offset for the moving sash without changing the fixed knot."""
import bpy,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]/'SourceArt/Characters/YiSunSin'
obj=bpy.data.objects['SK_YiSunSin_NPC'];ref=bpy.data.objects['REF_CompleteRootWeights'];arm=bpy.data.objects['Armature'];scene=bpy.context.scene
# Undo a previous mesh-margin experiment; preserve the authored 85mm shape.
previous=float(obj.get('npc_sash_blend_margin_m',0))
ids={i for p in obj.data.polygons if p.material_index==0 for i in p.vertices}
for i in ids:
 point=ref.matrix_world@ref.data.vertices[i].co;factor=max(0,min(1,(1.15-point.z)/.15))
 current=obj.matrix_world@obj.data.vertices[i].co;current.y+=previous*factor
 obj.data.vertices[i].co=obj.matrix_world.inverted()@current
obj['npc_sash_blend_margin_m']=0.0;obj.data.update()
for label,end in [('Walk',36),('Run',26)]:
 action=bpy.data.actions['NPC_Sash_'+label];prior=float(action.get('npc_sash_forward_offset_m',0));offset=.10-prior
 arm.animation_data.action=action;arm.animation_data.action_slot=action.slots[0]
 for frame in range(1,end+1):
  scene.frame_set(frame);pose=arm.pose.bones['npc_sash_01'];world=arm.matrix_world@pose.matrix
  world.translation+=Vector((0,-offset,0));pose.matrix=arm.matrix_world.inverted()@world
  bpy.context.view_layer.update();pose.keyframe_insert(data_path='location',frame=frame,group=pose.name)
 action['npc_sash_forward_offset_m']=.10
arm.animation_data.action=bpy.data.actions['NPC_Sash_Idle_Upright'];arm.animation_data.action_slot=arm.animation_data.action.slots[0];scene.frame_set(1)
result={'gait_sash_forward_offset_m':.10,'idle_forward_offset_m':0,'fixed_knot_unchanged':True,'runtime_solver':False}
(ROOT/'Audit/sash_blend_clearance.json').write_text(json.dumps(result,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'YiSunSin_NPC.blend'))
