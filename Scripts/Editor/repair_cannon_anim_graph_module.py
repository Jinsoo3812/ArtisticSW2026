"""Migrate the cannon graph wrapper without changing animation graph connections."""
import unreal

node_class = unreal.load_class(None, '/Script/ClassFeatureAnimGraph.AnimGraphNode_SWCannonOperator')
assert node_class
for name in ('Man', 'Woman'):
    path = '/Game/Anim_Logic/ABP_Player_' + name
    bp = unreal.load_asset(path)
    assert bp
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert bp.generated_class()
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    unreal.log('CANNON_ANIM_GRAPH_MIGRATED ' + path)
