"""Cold-load round trip for authored swim markers; never save assets."""
import hashlib
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/AudioIntegration/SwimContacts'
rows = json.loads((out / 'authored.json').read_text(encoding='utf-8'))
markers = 0
for row in rows:
    path = root / 'Content' / (row['package'].removeprefix('/Game/') + '.uasset')
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['saved_sha256']
    clip = unreal.load_asset(row['package'])
    actual = list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(clip))
    assert set(actual) == set(row['authored_notifies']), row['package']
    markers += len(row['contacts'])
assert len(rows) == 8 and markers == 16
(out / 'cold_verified.json').write_text(json.dumps({'clips': len(rows), 'markers': markers, 'saved_metadata_round_trip': True}, indent=2))
unreal.log('SW_SWIM_SAVED_CONTACTS_VERIFIED clips=8 markers=16')
