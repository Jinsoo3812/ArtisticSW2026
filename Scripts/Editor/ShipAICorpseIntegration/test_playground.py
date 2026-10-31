import unreal
import json
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
PLAYGROUND_OUTPUT_DIR=ROOT/'Saved/ShipAICorpseIntegration'
registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.search_all_assets(True)
contexts=registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/EnhancedInput','InputMappingContext'),True)
conflicts=[]
for data in contexts:
    context=data.get_asset()
    for mapping in context.get_editor_property('mappings'):
        key=str(unreal.InputLibrary.get_key_display_name(mapping.get_editor_property('key')))
        if key in ['F10','F11']:conflicts.append({'context':context.get_path_name(),'key':key})
(PLAYGROUND_OUTPUT_DIR/'input_audit.json').write_text(json.dumps({'contexts':len(contexts),'conflicts':conflicts,'keys':['Ctrl+Alt+Shift+F10','Ctrl+Alt+Shift+F11']},indent=2),encoding='utf8')
assert not conflicts,conflicts
source=ROOT/'Scripts/Editor/ChManRigValidation/test_player_ragdoll_playground.py'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
