import unreal
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
NETWORK_OUTPUT_DIR=ROOT/'Saved/ShipAICorpseIntegration'
source=ROOT/'Scripts/Editor/ChManRigValidation/run_ragdoll_network.py'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
