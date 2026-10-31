"""Run this Text once per Blender session to enable the ChWoman sidebar."""
import bpy
from mathutils import Vector,Matrix
def rig():return bpy.data.objects.get('Woman_CTRL')
def refresh():rig().update_tag();bpy.context.view_layer.update()
def names(limb):
    side=limb[-1]
    return [(s+'_'+side) for s in (['upperarm','lowerarm','hand'] if limb.startswith('arm') else ['thigh','calf','foot'])]
def match(limb,to_ik):
    r=rig();a=bpy.data.objects['Woman_EXPORT'];ns=names(limb);refresh()
    matrices={n:(r.matrix_world.inverted()@a.matrix_world@a.pose.bones[n].matrix).copy() for n in ns}
    if to_ik:
        p0,p1,p2=[matrices[n].translation for n in ns];axis=(p2-p0).normalized();bend=p1-(p0+axis*(p1-p0).dot(axis))
        if bend.length<.001:bend=r.pose.bones['POLE_'+limb].head-p1
        r.pose.bones['IK_'+limb].matrix=matrices[ns[2]];refresh()
        p=r.pose.bones['POLE_'+limb];mat=p.matrix.copy();mat.translation=p1+bend.normalized()*((p1-p0).length+(p2-p1).length)*.6;p.matrix=mat;refresh()
        r.pose.bones['FK_root']['ik_'+limb]=1.0;refresh()
        # Preserve authored FK axial twist while the IK chain owns joint heads.
        # Result bones are unweighted, export-only rotation adapters.
        for n in ns:
            p=r.pose.bones['RES_'+n]
            p.matrix=Matrix.LocRotScale(p.head.copy(),matrices[n].to_quaternion(),Vector((1,1,1)));refresh()
    else:
        r.pose.bones['FK_root']['ik_'+limb]=0.0;refresh()
        for n in ns:r.pose.bones['FK_'+n].matrix=matrices[n];refresh()
class CHW_OT_match(bpy.types.Operator):
    bl_idname='chw.match';bl_label='Match limb';bl_options={'REGISTER','UNDO'}
    limb:bpy.props.StringProperty();to_ik:bpy.props.BoolProperty()
    def execute(self,context):match(self.limb,self.to_ik);return {'FINISHED'}
class CHW_OT_weapon(bpy.types.Operator):
    bl_idname='chw.weapon';bl_label='Weapon reference';bl_options={'REGISTER','UNDO'}
    independent:bpy.props.BoolProperty()
    def execute(self,context):
        r=rig();a=bpy.data.objects['Woman_EXPORT'];refresh()
        if self.independent:r.pose.bones['FK_ik_hand_gun'].matrix=r.matrix_world.inverted()@a.matrix_world@a.pose.bones['ik_hand_gun'].matrix
        r.pose.bones['FK_root']['weapon_independent']=float(self.independent);refresh();return {'FINISHED'}
class CHW_OT_key(bpy.types.Operator):
    bl_idname='chw.key_controls';bl_label='Key visible selected controls';bl_options={'REGISTER','UNDO'}
    def execute(self,context):
        r=rig()
        for p in r.pose.bones:
            if not p.bone.select or p.name.startswith(('MCH_','RES_')):continue
            for path in ['location','rotation_euler','scale']:p.keyframe_insert(data_path=path,group=p.name)
        for prop in ['ik_arm_l','ik_arm_r','ik_leg_l','ik_leg_r','weapon_independent']:r.pose.bones['FK_root'].keyframe_insert(data_path='["'+prop+'"]',group='Rig Switches')
        for p in r.pose.bones:
            if p.name.startswith('RES_'):p.keyframe_insert(data_path='rotation_euler',group='IK Match Offsets')
        return {'FINISHED'}
class CHW_PT_controls(bpy.types.Panel):
    bl_label='ChWoman Controls';bl_idname='CHW_PT_controls';bl_space_type='VIEW_3D';bl_region_type='UI';bl_category='ChWoman'
    @classmethod
    def poll(cls,context):return rig() is not None
    def draw(self,context):
        layout=self.layout;r=rig();root=r.pose.bones['FK_root']
        layout.label(text='0 FK / 1 IK. Match before switching.')
        for limb in ['arm_l','arm_r','leg_l','leg_r']:
            layout.prop(root,'["ik_'+limb+'"]',text=limb,slider=True);row=layout.row()
            for label,mode in [('Match IK',True),('Match FK',False)]:op=row.operator('chw.match',text=label);op.limb=limb;op.to_ik=mode
        layout.label(text='Feet: FOOT + ROLL_heel / ROLL_toe')
        for label,mode in [('Snap independent weapon',True),('Follow wrist',False)]:layout.operator('chw.weapon',text=label).independent=mode
        layout.operator('chw.key_controls')
        row=layout.row();row.operator('chw.export_fbx',text='Export Bind FBX').animation=False;row.operator('chw.export_fbx',text='Bake Range FBX').animation=True
        layout.label(text='Eyes: LOOK_eyes + individual eye FK')
        layout.label(text='Jaw/lip references have no authored skin.')
        for name in ['Root & Body','Arms FK','Legs FK','Hands & Fingers','Face','Extras','IK Goals','IK Poles']:layout.prop(r.data.collections[name],'is_visible',text=name)
class CHW_OT_export(bpy.types.Operator):
    bl_idname='chw.export_fbx';bl_label='Export evaluated rig FBX'
    filepath:bpy.props.StringProperty(subtype='FILE_PATH')
    filter_glob:bpy.props.StringProperty(default='*.fbx',options={'HIDDEN'})
    check_existing:bpy.props.BoolProperty(default=True,options={'HIDDEN'})
    animation:bpy.props.BoolProperty(default=False)
    def invoke(self,context,event):
        import os
        self.filepath=os.path.join(os.path.dirname(bpy.data.filepath),'FBX','AS_ChWoman_Baked.fbx' if self.animation else 'SKM_ChWoman_UE5.fbx')
        context.window_manager.fileselect_add(self);return {'RUNNING_MODAL'}
    def execute(self,context):
        script=bpy.data.texts.get('ChWoman_FBX_Export.py')
        if not script:self.report({'ERROR'},'Missing bundled export Text');return {'CANCELLED'}
        settings={'EXPORT_ANIMATION':self.animation,'EXPORT_OUTPUT_PATH':self.filepath,'FRAME_START':context.scene.frame_start,'FRAME_END':context.scene.frame_end}
        exec(compile(script.as_string(),'ChWoman_FBX_Export.py','exec'),settings);return {'FINISHED'}
classes=[CHW_OT_match,CHW_OT_weapon,CHW_OT_key,CHW_OT_export,CHW_PT_controls]
for cls in classes:
    name=cls.__name__
    if issubclass(cls,bpy.types.Operator):
        prefix,operator=cls.bl_idname.split('.');name=prefix.upper()+'_OT_'+operator
    old=getattr(bpy.types,name,None)
    if old:bpy.utils.unregister_class(old)
    bpy.utils.register_class(cls)
