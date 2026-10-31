import unreal
from pathlib import Path
scripts=Path(__file__).parent
exec(compile((scripts/'audit_woman_final.py').read_text(encoding='utf8'),str(scripts/'audit_woman_final.py'),'exec'))
SAVED_CONNECTION_MODE=True;BP_PATH='/Game/Blueprints/Player/BP_Player_Woman'
EXPECTED_MESH='/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5.SKM_ChWoman_UE5';EXPECTED_ABP='/Game/Anim_Logic/ABP_Player_Woman.ABP_Player_Woman_C'
OUTPUT_DIR=Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation';SAVED_CASES=[('saved_woman_30',30,None,None),('saved_woman_60',60,None,None),('saved_woman_120',120,None,None)]
RESULT_FILENAME='final_saved_connection.json';FLOOR_Z=1000
source=scripts/'run_pie_comparison.py';exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'))
