import json
rows=[]
for sex in ['Man','Woman']:
    bp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_'+sex)
    ok=unreal.SWCannonOperatorAuthoringLibrary.install_operator_overlay(bp)
    rows.append({'sex':sex,'installed':ok})
    assert ok, str(rows)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
m=unreal.load_asset('/Game/Blueprints/Ship/Cannon/Mesh/SM_Cannon')
result={'animations':rows,'cannon_bounds':str(m.get_bounding_box())}
(OUT/'cannon_author.json').write_text(json.dumps(result,indent=2))
