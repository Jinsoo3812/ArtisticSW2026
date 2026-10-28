"""Prepare exported audio for UE import; requires numpy. Originals remain untouched.

python prepare_audio_wavs.py --staging Saved/AudioIntegration --cannon-zip <zip>
"""
import argparse
import hashlib
import io
import json
import struct
from pathlib import Path
import wave
import zipfile
import numpy as np


def decode(data):

    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Expected RIFF WAVE")
    chunks = {}
    offset = 12
    while offset + 8 <= len(data):
        kind, size = struct.unpack_from("<4sI", data, offset)
        chunks[kind] = data[offset + 8:offset + 8 + size]
        offset += 8 + size + (size & 1)
    encoding, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", chunks[b"fmt "])
    width = bits // 8
    raw = chunks[b"data"]
    if encoding == 3 and width == 4:
        samples = np.frombuffer(raw, dtype="<f4").copy()
    elif encoding not in (1, 65534):
        raise ValueError("Unsupported WAV encoding: " + str(encoding))
    elif width == 2:
        samples = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 3:
        packed = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        integers = packed[:, 0] | (packed[:, 1] << 8) | (packed[:, 2] << 16)
        integers = (integers ^ 0x800000) - 0x800000
        samples = integers.astype(np.float32) / 8388608.0
    elif width == 4:
        samples = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
    else:
        raise ValueError("Unsupported sample width: " + str(width))
    return samples.reshape(-1, channels), rate


def prepare(data, dest, loop=False, cannon=False, mono=False):
    samples, rate = decode(data)
    if mono:
        samples = samples.mean(axis=1, keepdims=True)
    original_frames = len(samples)
    if not loop:
        # Positional events use mono originals; baked binaural recordings are excluded.
        samples = samples.mean(axis=1, keepdims=True)
        active = np.flatnonzero(np.max(np.abs(samples), axis=1) > (0.002 if cannon else 0.001))
        if not len(active):
            raise ValueError("Silent input: " + str(dest))
        start = max(0, int(active[0]) - int(rate * 0.005))
        if cannon:
            # These location recordings contain handling/fuse sounds before the blast.
            # Align the dominant blast onset with the accepted gameplay shot.
            envelope = np.max(np.abs(samples), axis=1)
            peak_frame = int(np.argmax(envelope))
            window_start = max(0, peak_frame - int(rate * 0.3))
            attack = np.flatnonzero(envelope[window_start:peak_frame + 1] > envelope[peak_frame] * 0.25)
            start = max(0, window_start + int(attack[0]) - int(rate * 0.008))
        end = min(len(samples), int(active[-1]) + int(rate * 0.12))
        samples = samples[start:end].copy()
        fade = min(len(samples) // 4, int(rate * 0.003))
        samples[:fade] *= np.linspace(0, 1, fade)[:, None]
        samples[-fade:] *= np.linspace(1, 0, fade)[:, None]
    else:
        # Equal-power crossfade of the end onto the beginning, then remove the overlap.
        fade = min(int(rate * 0.12), len(samples) // 10)
        t = np.linspace(0, np.pi / 2, fade)[:, None]
        samples[:fade] = samples[-fade:] * np.cos(t) + samples[:fade] * np.sin(t)
        samples = samples[:-fade]
    peak = float(np.max(np.abs(samples)))
    rms = float(np.sqrt(np.mean(samples * samples)))
    # Preserve dynamic shape. No compressor/limiter; only bounded gain and headroom.
    gain = min(3.0, (0.89 / max(peak, 1e-8)), (0.13 if cannon else (0.10 if loop else 0.08)) / max(rms, 1e-8))
    samples *= gain
    encoded = np.clip(samples * 32767, -32768, 32767).astype("<i2")
    dest.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(dest), "wb") as wav:
        wav.setnchannels(samples.shape[1])
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(encoded.tobytes())
    return {"seconds": len(samples) / rate, "source_seconds": original_frames / rate,
            "channels": samples.shape[1], "sample_rate": rate, "gain": gain,
            "peak": float(np.max(np.abs(samples))), "rms": float(np.sqrt(np.mean(samples * samples))),
            "loop": loop, "sha256": hashlib.sha256(dest.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--staging", type=Path, required=True)
    parser.add_argument("--cannon-zip", type=Path, required=True)
    args = parser.parse_args()
    staging = args.staging.resolve()
    output = staging / "Prepared"
    report = []
    exported = json.loads((staging / "SourceWav/export_manifest.json").read_text(encoding="utf-8"))
    for entry in exported:
        relative = Path(entry["file"])
        dest = output / relative
        meta = prepare((staging / "SourceWav" / relative).read_bytes(), dest, relative.name.startswith("Loop_"))
        report.append({"file": str(relative), "source": entry["source"], **meta})
    with zipfile.ZipFile(args.cannon_zip) as archive:
        for number in [1, 3, 5, 8, 12, 16]:
            name = "Pirate Ship Cannon " + str(number) + ".wav"
            relative = Path("Cannon") / ("SW_Cannon_" + str(number).zfill(2) + ".wav")
            meta = prepare(archive.read(name), output / relative, cannon=True)
            report.append({"file": str(relative), "source": "cannon_sounds.zip/" + name, **meta})
    (staging / "prepared_manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"prepared": len(report), "bytes": sum((output / r["file"]).stat().st_size for r in report),
                      "max_peak": max(r["peak"] for r in report),
                      "cannons": [r for r in report if r["file"].startswith("Cannon")]}, indent=2))


if __name__ == "__main__":
    main()
