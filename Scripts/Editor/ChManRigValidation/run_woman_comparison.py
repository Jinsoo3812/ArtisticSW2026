import unreal
from pathlib import Path
SAVED_CONNECTION_MODE=True
BP_PATH='/Game/Blueprints/Player/BP_Player_Woman'
OUTPUT_DIR=Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation'
SAVED_CASES=[
    ('old_woman_offsets',60,'/Game/jiwon/Characters/SKM_Player_Woman','/Game/Anim_Logic/ABP_Player_Woman'),
    ('new_woman_offsets',60,'/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5','/Game/Anim_Logic/ABP_Player_Woman'),
    ('new_woman_clean_60',60,'/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5','/Game/Developers/ChWomanRigValidation/ABP_Woman_NoFixedRigOffsets'),
    ('new_woman_clean_30',30,'/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5','/Game/Developers/ChWomanRigValidation/ABP_Woman_NoFixedRigOffsets'),
    ('new_woman_clean_120',120,'/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5','/Game/Developers/ChWomanRigValidation/ABP_Woman_NoFixedRigOffsets')]
RESULT_FILENAME='pie_comparison.json';FLOOR_Z=1000
source=Path(__file__).with_name('run_pie_comparison.py')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'))
