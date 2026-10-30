"""Use the accepted two-player fall probe with ship audio excluded in PIE only."""
from pathlib import Path
f = live_api['network_fixture']
for w in f['worlds']:
    for ship in f['allof'](w, unreal.Ship):
        ship.set_actor_hidden_in_game(True)
OUT = OUT / 'CharacterEntryWeightVerified'
OUT.mkdir(exist_ok=True)
script = Path(unreal.Paths.project_dir()) / 'Scripts/Editor/AudioPIE/network_entries.py'
exec(compile(script.read_text(encoding='utf-8-sig'), str(script), 'exec'), globals())
