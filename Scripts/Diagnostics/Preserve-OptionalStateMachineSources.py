"""Preserve recovered experimental definitions without changing production MM hints."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/AssetRepair'
base=Path('Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData')
dest_root=ROOT/'SourceArt/AssetRecovery/UnusedStateMachine'
assert dest_root.resolve().is_relative_to(ROOT.resolve())
rows=[]
for name in ['PSD_SM_CMC_Idles.uasset','PSS_SM_CMC_Idles.uasset']:
    source=ROOT/'Content'/base/name;destination=dest_root/name
    assert source.is_file() and source.resolve().is_relative_to((ROOT/'Content').resolve()) and not destination.exists()
    data=source.read_bytes();destination.parent.mkdir(parents=True,exist_ok=True);source.replace(destination)
    assert hashlib.sha256(destination.read_bytes()).digest()==hashlib.sha256(data).digest()
    rows.append({'source':str(source.relative_to(ROOT)),'preserved':str(destination.relative_to(ROOT)),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
shutil.copy2(OUT/'original_idle_schema.json',dest_root/'OriginalSchemaSettings.json')
(OUT/'optional_state_machine_sources.json').write_text(json.dumps({'preserved':rows,'reason':'production MM uses project-owned databases; unresolved prototype hints must retain their existing null semantics, not introduce new search databases'},indent=2),encoding='utf8')
