"""Prepare existing water recordings for positional ship/impact playback."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import wave
import numpy as np
from prepare_audio_wavs import decode, prepare

parser = argparse.ArgumentParser()
parser.add_argument("--staging", type=Path, required=True)
args = parser.parse_args()
source = args.staging / "ShipWaterSources"
dest = args.staging / "ShipWaterPrepared"
dest.mkdir(parents=True, exist_ok=True)
manifest = {Path(r["file"]).stem: r for r in json.loads((source / "export_manifest.json").read_text())}
rows = []
for name in ["Loop_River_Ambience__Boat1", "Loop_Running_Water2", "Oneshot_Splash3", "Oneshot_Splash5"]:
    path = dest / (name + ".wav")
    meta = prepare((source / (name + ".wav")).read_bytes(), path, loop=name.startswith("Loop_"), mono=True)
    rows.append({"file": path.name, "source": manifest[name]["source"], **meta})

# A low water-body layer comes from the existing splash itself, not an explosion
# or generated sub-bass. Keep the separate sharp attack recordings unchanged.
samples, rate = decode((source / "Oneshot_Splash6.wav").read_bytes())
samples = samples.mean(axis=1)
length = 257
n = np.arange(length) - (length - 1) / 2
cutoff = 650.0 / rate
kernel = 2 * cutoff * np.sinc(2 * cutoff * n) * np.hamming(length)
kernel /= kernel.sum()
body = np.convolve(samples, kernel, mode="same")
data = io.BytesIO()
with wave.open(data, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(np.clip(body * 32767, -32768, 32767).astype("<i2").tobytes())
path = dest / "WaterImpact_Body.wav"
meta = prepare(data.getvalue(), path, mono=True)
rows.append({"file": path.name, "source": manifest["Oneshot_Splash6"]["source"], "processing": "650Hz FIR water-body layer", **meta})
extra = args.staging / 'ShipWaveSources'
extra_manifest = {Path(r['file']).stem:r for r in json.loads((extra/'export_manifest.json').read_text())}
for name in ['Oneshot_Swimming_Heavy1','Oneshot_Wave_Crash4']:
    path=dest/(name+'.wav')
    rows.append({'file':path.name,'source':extra_manifest[name]['source'],**prepare((extra/(name+'.wav')).read_bytes(),path,mono=True)})

def trimmed(folder,name,pitch=1.):
    a,r=decode((folder/(name+'.wav')).read_bytes());a=a.mean(axis=1)
    active=np.flatnonzero(abs(a)>.001)
    a=a[max(0,active[0]-int(r*.005)):min(len(a),active[-1]+int(r*.1))]
    # Resampling lowers the recorded water body without synthesizing sub-bass.
    a=np.interp(np.arange(0,len(a),pitch),np.arange(len(a)),a)
    a=a/(float(np.sqrt(np.mean(a*a)))+1e-9)*.08
    return a,r

def write_composite(name,layers,description):
    rate=44100
    signals=[]
    for folder,recording,pitch,gain,delay in layers:
        a,r=trimmed(folder,recording,pitch)
        assert r==rate
        signals.append((a*gain,int(delay*rate)))
    mixed=np.zeros(max(len(a)+offset for a,offset in signals))
    for a,offset in signals:mixed[offset:offset+len(a)]+=a
    fade=min(int(rate*.02),len(mixed)//10)
    mixed[:fade]*=np.linspace(0,1,fade);mixed[-fade:]*=np.linspace(1,0,fade)
    buf=io.BytesIO()
    with wave.open(buf,'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate)
        w.writeframes(np.clip(mixed*32767,-32768,32767).astype('<i2').tobytes())
    path=dest/(name+'.wav')
    rows.append({'file':path.name,'sources':[str(folder/(recording+'.wav')) for folder,recording,_,_,_ in layers],'processing':description,**prepare(buf.getvalue(),path,mono=True)})

write_composite('CannonWaterBody',[(source,'Oneshot_Splash6',.82,.65,0),(extra,'Oneshot_Diving_Underwater1',.82,.75,.035)],'Dense splash and deeper underwater plunge, 35ms follow-through')
(dest / "prepared_manifest.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
from prepare_character_entry import prepare_character_entry
prepare_character_entry(args.staging)
rows = json.loads((dest / "prepared_manifest.json").read_text(encoding="utf-8"))
print(json.dumps(rows, indent=2))
