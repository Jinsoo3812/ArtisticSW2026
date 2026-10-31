import unreal
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
PLAYGROUND_OUTPUT_DIR=ROOT/'Saved/ShipAICorpseIntegration'
source=ROOT/'Scripts/Editor/ChManRigValidation/test_player_ragdoll_playground.py'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
