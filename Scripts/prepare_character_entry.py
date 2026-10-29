"""Build full-body character plunges from the owned diving/splash recordings."""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import numpy as np
from prepare_audio_wavs import decode


def prepare_character_entry(staging, cannon=False):
    source = staging / 'ShipWaterSources'
    extra = staging / 'ShipWaveSources'
    dest = staging / 'ShipWaterPrepared'; dest.mkdir(parents=True, exist_ok=True)
    manifest = dest / 'prepared_manifest.json'
    rows = json.loads(manifest.read_text()) if manifest.exists() else []
    rate = 44100

    def layer(folder, name, pitch):
        a, r = decode((folder / (name + '.wav')).read_bytes())
        assert r == rate
        a = a.mean(axis=1)
        # Align the first substantial water attack, preserving a 5ms lead-in.
        window = int(rate * .01)
        envelope = np.sqrt(np.convolve(a * a, np.ones(window) / window, mode='same'))
        active = np.flatnonzero(envelope > envelope.max() * .12)
        assert len(active)
        end = min(len(a), active[-1] + int(rate * .18))
        a = a[max(0, active[0] - int(rate * .005)):end]
        a = np.interp(np.arange(0, len(a), pitch), np.arange(len(a)), a)
        # Equalize source layers before combining; the final transient retains headroom.
        a *= min(3., .12 / (float(np.sqrt(np.mean(a * a))) + 1e-9))
        return a

    recipes = {
        'CharacterEntry_PlungeA': [(source, 'Oneshot_Diving1', .96, .9, 0.), (source, 'Oneshot_Splash4', .96, .4, .008)],
        'CharacterEntry_PlungeB': [(source, 'Oneshot_Diving2', .98, .9, 0.), (source, 'Oneshot_Splash2', .94, .4, .008)],
        'CharacterWaterBody': [(extra, 'Oneshot_Diving_Underwater1', .94, 1., .045), (source, 'Oneshot_Splash6', .9, .6, .025)],
    }
    if cannon:
        recipes = {
            'CannonEntry_PlungeA': [(source, 'Oneshot_Splash4', .9, 1., 0.), (source, 'Oneshot_Diving1', .94, .4, .008)],
            'CannonEntry_PlungeB': [(source, 'Oneshot_Splash2', .9, 1., 0.), (source, 'Oneshot_Diving2', .94, .4, .008)],
            'CannonWaterBody': [(extra, 'Oneshot_Diving_Underwater1', .88, 1., .025), (source, 'Oneshot_Splash6', .88, .7, .015)],
        }
    changed = []
    for name, recipe in recipes.items():
        layers = [(layer(folder, clip, pitch) * gain, int(delay * rate)) for folder, clip, pitch, gain, delay in recipe]
        mixed = np.zeros(max(len(a) + offset for a, offset in layers))
        for a, offset in layers: mixed[offset:offset + len(a)] += a
        if cannon: mixed = mixed[:int(rate * 1.2)]
        fade = min(int(rate * .003), len(mixed) // 10)
        mixed[:fade] *= np.linspace(0, 1, fade)
        tail = min(int(rate * .12), len(mixed) // 10)
        mixed[-tail:] *= np.linspace(1, 0, tail)
        target = .12 if name == 'CharacterWaterBody' else .115
        gain = min(2., target / (np.sqrt(np.mean(mixed * mixed)) + 1e-9), .82 / (abs(mixed).max() + 1e-9))
        mixed *= gain
        path = dest / (name + '.wav')
        with wave.open(str(path), 'wb') as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(rate)
            wav.writeframes(np.round(mixed * 32767).astype('<i2').tobytes())
        row = {'file': path.name, 'loop': False, 'channels': 1, 'seconds': len(mixed) / rate,
               'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'peak': float(abs(mixed).max()),
               'rms': float(np.sqrt(np.mean(mixed * mixed))),
               'sources': [str(folder / (clip + '.wav')) for folder, clip, _, _, _ in recipe],
               'processing': 'Full-body diving transient and displaced water; bounded linear gain, no synthesized bass'}
        rows = [old for old in rows if old['file'] != path.name]
        rows.append(row); changed.append(row)
    manifest.write_text(json.dumps(rows, indent=2), encoding='utf-8')
    return changed


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--staging', type=Path, required=True)
    print(json.dumps(prepare_character_entry(parser.parse_args().staging), indent=2))
