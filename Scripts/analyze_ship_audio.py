"""Summarize each isolated PIE device recording without assuming silence duration."""
import argparse
import json
from pathlib import Path
import wave
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
args=parser.parse_args()
rows=[]
for path in sorted(args.directory.glob('network_verified_*.wav')):
    with wave.open(str(path),'rb') as wav:
        channels,rate,width=wav.getnchannels(),wav.getframerate(),wav.getsampwidth()
        assert width==2,(path,width)
        samples=np.frombuffer(wav.readframes(wav.getnframes()),dtype='<i2').astype(float)/32768
    mono=samples.reshape(-1,channels).mean(axis=1)
    rows.append({'file':path.name,'rate':rate,'channels':channels,'seconds':len(mono)/rate,
                 'rms':float(np.sqrt(np.mean(samples**2))) if len(samples) else 0.,
                 'peak':float(np.max(np.abs(samples))) if len(samples) else 0.,
                 'clipped_samples':int(np.count_nonzero(np.abs(samples)>=.999)),
                 'active_samples':int(np.count_nonzero(np.abs(samples)>1/32768))})
target=args.directory/'network_signal_summary.json'
target.write_text(json.dumps(rows,indent=2),encoding='utf-8')
print(json.dumps(rows,indent=2))
