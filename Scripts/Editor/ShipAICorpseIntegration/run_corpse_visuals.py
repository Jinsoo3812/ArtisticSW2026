"""Saved ShipAI-derived BP/ABP: idle/run/sprint/jump/moving-deck physics and skin."""
import unreal
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
REVIEW_CASES=[(label,30,mode) for label in ['Man','Woman'] for mode in ['idle','run','sprint','jump','platform']]
REVIEW_FILENAME='corpse_visuals.json'
POSE_OUTPUT_DIR=ROOT/'Saved/ShipAICorpseIntegration'
source=ROOT/'Scripts/Editor/ChManRigValidation/run_player_pose_fix.py'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
