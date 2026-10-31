"""Saved latest male and female BP/ABP through the existing twelve-phase PIE probe."""
import unreal
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
SAVED_CONNECTION_MODE=True;FLOOR_Z=1000
BP_PATH='/Game/Blueprints/Player/BP_Player_Man'
EXPECTED_MESH='/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5.SKM_ChMan_UE5'
EXPECTED_ABP='/Game/Anim_Logic/ABP_Player_Man.ABP_Player_Man_C'
OUTPUT_DIR=ROOT/'Saved/ShipAICorpseIntegration'
SAVED_CASES=[('Man',60,None,None),('Woman',60,None,None)]
RESULT_FILENAME='motion_matching.json'
source=ROOT/'Scripts/Editor/ChManRigValidation/run_pie_comparison.py'
text=source.read_text(encoding='utf8').replace('RUNNER=Runner()','')
exec(compile(text,str(source),'exec'))
class BothCharacters(Runner):
    def next_case(self):
        next_index=self.case_index+1
        if next_index<len(SAVED_CASES):
            label=SAVED_CASES[next_index][0]
            self.bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_'+label)
            globals()['EXPECTED_MESH']='/Game/jiwon/Characters/Ch'+label+'UE5/SKM_Ch'+label+'_UE5.SKM_Ch'+label+'_UE5'
            globals()['EXPECTED_ABP']='/Game/Anim_Logic/ABP_Player_'+label+'.ABP_Player_'+label+'_C'
        super().next_case()
RUNNER=BothCharacters()
