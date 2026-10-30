"""Run with Blender --python on a fresh scene after UE reference FBX export.

Does not delete existing scene content. Refuses to overwrite an existing NPC rig.
"""
import bpy
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parents[1] / 'SourceArt/Characters/YiSunSin'
if (ROOT/'YiSunSin_NPC.blend').exists() and '--replace-generated-yisunsin' not in sys.argv:
    raise RuntimeError('Preserve the authored blend; explicit --replace-generated-yisunsin is required to rebuild it')
if any(bpy.data.objects.get(name) for name in ['spine', 'Armature', 'SK_YiSunSin_NPC']):
    raise RuntimeError('Use a fresh Blender scene to preserve the current work')
for filename, collection_name, animation in [
    ('SK_YiSunSin_Latest.fbx', '01_Source_Reference', False),
    ('AS_YiSunSin_Walk.fbx', '02_Animation_Reference', True),
]:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT/'Reference'/filename), use_anim=animation,
                            automatic_bone_orientation=False, ignore_leaf_bones=False)
    collection = bpy.data.collections.new(collection_name)
    bpy.context.scene.collection.children.link(collection)
    for obj in set(bpy.data.objects)-before:
        for old in list(obj.users_collection):
            old.objects.unlink(obj)
        collection.objects.link(obj)
if bpy.data.collections.get('Collection'):
    bpy.data.collections['Collection'].hide_viewport = True
    bpy.data.collections['Collection'].hide_render = True
for filename in ['prepare_yisunsin_npc.py', 'fit_yisunsin_garments.py',
                 'validate_yisunsin_npc.py', 'author_yisunsin_idle.py',
                 'export_yisunsin_npc.py', 'author_yisunsin_sash.py',
                 'fit_yisunsin_sash_motion.py', 'finish_yisunsin_sash.py', 'export_yisunsin_sash.py']:
    script = TOOLS/filename
    namespace = {'__name__': '__main__', '__file__': str(script)}
    exec(compile(script.read_text(encoding='utf-8-sig'), str(script), 'exec'), namespace)
result = {'blend_file': str(ROOT/'YiSunSin_NPC.blend'), 'pipeline_complete': True}
