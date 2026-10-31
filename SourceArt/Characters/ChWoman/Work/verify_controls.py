"""Exercise the delivered controls, preserving the user's pose/animation state."""
import bpy,json,math
from pathlib import Path
from mathutils import Matrix,Vector
base=Path(r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman')
exec(compile((base/'Work/rig_controls_ui.py').read_text(encoding='utf8'),'rig_controls_ui.py','exec'))
r=rig();a=bpy.data.objects['Woman_EXPORT'];m=bpy.data.objects['SKM_ChWoman_UE5']
assert not r.animation_data or not r.animation_data.action,'Use a separate test copy if an Action is active'
saved={p.name:p.matrix_basis.copy() for p in r.pose.bones};props={k:r.pose.bones['FK_root'][k] for k in ['ik_arm_l','ik_arm_r','ik_leg_l','ik_leg_r','weapon_independent']}
audit={'limbs':{},'extra_controls':{}}
def reset():
    for p in r.pose.bones:p.matrix_basis.identity()
    for key in ['arm_l','arm_r','leg_l','leg_r']:r.pose.bones['FK_root']['ik_'+key]=0.0
    r.pose.bones['FK_root']['weapon_independent']=0.0;refresh()
def angle(a,b):return math.degrees(2*math.acos(min(1,abs(a.normalized().dot(b.normalized())))))
def record(before,ns):return {'max_head_error_cm':max((a.pose.bones[n].head-before[n].translation).length*100 for n in ns),'max_rotation_error_deg':max(angle(a.pose.bones[n].matrix.to_quaternion(),before[n].to_quaternion()) for n in ns)}
try:
    for limb in ['arm_l','arm_r','leg_l','leg_r']:
        reset();ns=names(limb);r.pose.bones['FK_'+ns[0]].rotation_euler=(.2,.1,.15);r.pose.bones['FK_'+ns[1]].rotation_euler=(.1,.3,.1);refresh()
        before={n:a.pose.bones[n].matrix.copy() for n in ns};match(limb,True);row={'fk_to_ik':record(before,ns)}
        target=r.pose.bones['IK_'+limb];mat=target.matrix.copy();mat.translation+=Vector((0,-.05,.03));target.matrix=mat;refresh()
        row['goal_error_cm']=(a.pose.bones[ns[2]].head-target.head).length*100
        row['length_error_cm']=max(abs((a.pose.bones[ns[i+1]].head-a.pose.bones[ns[i]].head).length-(a.data.bones[ns[i+1]].head_local-a.data.bones[ns[i]].head_local).length)*100 for i in [0,1])
        before={n:a.pose.bones[n].matrix.copy() for n in ns};match(limb,False);row['ik_to_fk']=record(before,ns);audit['limbs'][limb]=row
        assert row['goal_error_cm']<.01 and row['length_error_cm']<.01
        assert row['fk_to_ik']['max_head_error_cm']<.01 and row['ik_to_fk']['max_head_error_cm']<.01
        assert row['fk_to_ik']['max_rotation_error_deg']<.1 and row['ik_to_fk']['max_rotation_error_deg']<.1
    reset();root=r.pose.bones['FK_root'];root.location=(.25,-.1,.1);root.rotation_euler.z=.5;refresh()
    audit['root_head_error_cm']=(a.pose.bones['root'].head-root.head).length*100
    audit['marker_error_cm']=max((a.pose.bones[x].head-a.pose.bones[y].head).length*100 for x,y in [('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_foot_l','foot_l'),('ik_foot_r','foot_r')])
    assert audit['root_head_error_cm']<.001 and audit['marker_error_cm']<.001
    for control in ['ROLL_heel_l','ROLL_toe_r','FK_index_01_l','FK_cc_base_l_eye','LOOK_eyes']:
        reset()
        if control.startswith('ROLL_'):match('leg_'+control[-1],True)
        before=[v.co.copy() for v in m.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
        r.pose.bones[control].rotation_euler.x=.2;refresh();after=m.evaluated_get(bpy.context.evaluated_depsgraph_get())
        delta=max((v.co-before[v.index]).length*100 for v in after.data.vertices);audit['extra_controls'][control]={'max_skin_motion_cm':delta};assert delta>.001
    reset();before=a.pose.bones['ik_hand_gun'].matrix.copy();bpy.ops.chw.weapon(independent=True);refresh()
    audit['independent_weapon_head_error_cm']=(a.pose.bones['ik_hand_gun'].head-before.translation).length*100
    assert audit['independent_weapon_head_error_cm']<.001
    audit['passed']=True
finally:
    for p in r.pose.bones:p.matrix_basis=saved[p.name]
    for key,value in props.items():r.pose.bones['FK_root'][key]=value
    refresh()
(base/'Work/control_validation.json').write_text(json.dumps(audit,indent=2),encoding='utf8');result=audit
