"""Build the audited cabin-pillar camera proxy, preserving the gameplay mesh.

Run inside UE 5.7 outside PIE. The exact source hash and 16 reviewed pillar
bounds are in docs/validation/Ship_Camera_Pillar_Selection.json. Does not save maps.
"""
import hashlib
import json
from pathlib import Path
import unreal


def author():
    root = Path(unreal.Paths.project_dir()).resolve()
    cfg = json.loads((root / 'docs/validation/Ship_Camera_Pillar_Selection.json').read_text())
    source_file = root / 'Content' / (cfg['source_asset'].removeprefix('/Game/') + '.uasset')
    assert hashlib.sha256(source_file.read_bytes()).hexdigest() == cfg['source_sha256'], 'Source changed; re-audit selection'
    source = unreal.load_asset(cfg['source_asset'])
    source_desc = source.get_static_mesh_description(0)
    assert source_desc.get_triangle_count() == cfg['source_triangles']
    mesh = unreal.load_asset(cfg['proxy_asset']) if unreal.EditorAssetLibrary.does_asset_exist(cfg['proxy_asset']) else None
    if mesh:
        assert mesh.get_static_mesh_description(0).get_triangle_count() == cfg['source_triangles'] - cfg['removed_triangles'], 'Unexpected existing proxy'
    else:
        mesh = unreal.SWShipCameraAuthoringLibrary.build_camera_proxy(
            source, cfg['proxy_asset'],
            [unreal.Vector(*p['min']) for p in cfg['pillars']],
            [unreal.Vector(*p['max']) for p in cfg['pillars']], cfg['removed_triangles'])
        assert mesh, 'Audited camera proxy build failed'
        assert mesh.get_static_mesh_description(0).get_triangle_count() == cfg['source_triangles'] - cfg['removed_triangles']
        assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)

    bp = unreal.load_asset(cfg['blueprint'])
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    functions = unreal.SubobjectDataBlueprintFunctionLibrary

    def objects():
        found = {}
        for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
            data = subsystem.k2_find_subobject_data_from_handle(handle)
            obj = functions.get_object_for_blueprint(data, bp)
            if obj:
                found[obj.get_name().removesuffix('_GEN_VARIABLE')] = (handle, obj)
        return found

    parts = objects()
    name = 'CameraObstacleMesh'
    if name not in parts:
        handle, failure = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
            parent_handle=parts['BuoyancyRoot'][0], new_class=unreal.StaticMeshComponent, blueprint_context=bp))
        assert functions.is_handle_valid(handle), str(failure)
        assert subsystem.rename_subobject(handle, unreal.Text(name))
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        parts = objects()
    component = parts[name][1]
    component.modify()
    component.set_editor_property('static_mesh', mesh)
    component.set_relative_transform(unreal.Transform(), False, True)
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    component.set_collision_object_type(unreal.CollisionChannel.ECC_WORLD_DYNAMIC)
    component.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_BLOCK)
    component.set_editor_property('generate_overlap_events', False)
    component.set_visibility(False, False)
    component.set_hidden_in_game(True, False)
    component.set_cast_shadow(False)
    component.set_editor_property('component_tags', ['SW.Camera.Block'])
    original = parts['DeckMesh_Simple'][1]
    original.modify()
    tags = list(original.component_tags)
    if 'SW.Camera.Ignore' not in [str(t) for t in tags]:
        tags.append('SW.Camera.Ignore')
    original.set_editor_property('component_tags', tags)
    bp.modify()
    # Reinstance placed ships after configuring the SCS template. Saving alone
    # leaves already constructed actors with the pre-configuration empty mesh.
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    assert hashlib.sha256(source_file.read_bytes()).hexdigest() == cfg['source_sha256']
    return {'blueprint': cfg['blueprint'], 'proxy': mesh.get_path_name(),
            'source_triangles': cfg['source_triangles'], 'proxy_triangles': mesh.get_static_mesh_description(0).get_triangle_count(),
            'removed_pillars': len(cfg['pillars']), 'removed_triangles': cfg['removed_triangles'], 'original_source_unchanged': True}


if __name__ == '__main__':
    unreal.log('SW_SHIP_CAMERA_AUTHOR ' + json.dumps(author()))
