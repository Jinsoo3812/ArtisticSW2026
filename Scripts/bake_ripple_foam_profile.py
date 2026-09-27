"""Generate the normalized Ripple foam profile and Golden diagnostics.

Runtime reuses the equivalent analytic phase/envelope values in SWRipple.ush;
the baked profile is the deterministic art/validation reference.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


def smoothstep01(value: np.ndarray) -> np.ndarray:
    value = np.clip(value, 0.0, 1.0)
    return value * value * (3.0 - 2.0 * value)


def main() -> None:
    project_dir = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--resolution", type=int, default=256)
    parser.add_argument("--crest-sharpness", type=float, default=6.0)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=project_dir / "Content/Blueprints/Water/Data/Ripple",
    )
    parser.add_argument(
        "--diagnostic-dir",
        type=Path,
        default=project_dir / "Saved/Diagnostics/SWFoam/Ripple",
    )
    args = parser.parse_args()

    if args.resolution < 16:
        raise ValueError("Ripple profile resolution must be at least 16")

    # Signed front distance normalized by wavelength, matching SWRipple.ush.
    coordinate = np.linspace(-2.0, 2.0, args.resolution, dtype=np.float64)
    abs_from_front = np.abs(coordinate) * 0.5
    envelope = 1.0 - smoothstep01(abs_from_front)
    phase = coordinate * (2.0 * np.pi)
    cosine = np.cos(phase)
    sine = np.sin(phase)
    height = cosine * envelope

    envelope_derivative = -(6.0 * abs_from_front * (1.0 - abs_from_front) * 0.5)
    envelope_derivative *= np.sign(coordinate)
    radial_derivative = -sine * (2.0 * np.pi) * envelope + cosine * envelope_derivative
    foam_location = np.power(np.clip(cosine, 0.0, 1.0), args.crest_sharpness) * envelope

    profile = np.stack((height, radial_derivative, foam_location, envelope), axis=-1)
    if not np.all(np.isfinite(profile)):
        raise AssertionError("Non-finite Ripple profile")
    symmetry_error = float(np.max(np.abs(foam_location - foam_location[::-1])))
    if symmetry_error > 1.0e-10:
        raise AssertionError(f"Ripple profile symmetry regression: {symmetry_error}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_path = args.output_dir / "ripple_foam_profile_rgba16f.bin"
    payload = profile.astype(np.float16).tobytes()
    output_path.write_bytes(payload)

    args.diagnostic_dir.mkdir(parents=True, exist_ok=True)
    image_row = np.clip(foam_location * 255.0, 0.0, 255.0).astype(np.uint8)[None, :]
    preview = np.repeat(image_row, 64, axis=0)
    Image.fromarray(preview, mode="L").save(args.diagnostic_dir / "ripple_foam_profile.png")

    report = {
        "version": "SWImprovedFoam_RippleProfile_1",
        "resolution": args.resolution,
        "crest_sharpness": args.crest_sharpness,
        "coordinate_min": -2.0,
        "coordinate_max": 2.0,
        "format": "RGBA16F",
        "channels": ["height", "radial_derivative", "foam_location", "envelope"],
        "sha256": hashlib.sha256(payload).hexdigest(),
        "foam_min": float(foam_location.min()),
        "foam_mean": float(foam_location.mean()),
        "foam_max": float(foam_location.max()),
        "coverage_gt_0_05": float(np.mean(foam_location > 0.05)),
        "symmetry_max_abs": symmetry_error,
    }
    report_path = args.diagnostic_dir / "ripple_foam_profile_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
