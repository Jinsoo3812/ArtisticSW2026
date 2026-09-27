"""Bake Kelvin Foam LocationMask into Golden RGBA16F channel A.

R/G/B are preserved byte-for-byte.  The generated A channel is an authorable,
source-local emission-location mask; it is not a Tessendorf folding map.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image


RES_U = 512
RES_V = 256
FILENAMES = (
    "kelvin_wake_golden_fr030_fp16.bin",
    "kelvin_wake_golden_fr050_fp16.bin",
    "kelvin_wake_golden_fr070_fp16.bin",
    "kelvin_wake_golden_fr100_fp16.bin",
)


def smoothstep(edge0: float, edge1: float, value: np.ndarray) -> np.ndarray:
    t = np.clip((value - edge0) / max(edge1 - edge0, 1.0e-8), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def bake_file(
    source: Path,
    baseline_dir: Path,
    preview_dir: Path,
    height_start: float,
    height_full: float,
    gradient_width: float,
    gamma: float,
) -> dict:
    raw_before = source.read_bytes()
    expected_bytes = RES_U * RES_V * 4 * np.dtype(np.float16).itemsize
    if len(raw_before) != expected_bytes:
        raise ValueError(f"Unexpected Golden size: {source} ({len(raw_before)} != {expected_bytes})")

    baseline_dir.mkdir(parents=True, exist_ok=True)
    baseline = baseline_dir / source.name
    if not baseline.exists():
        shutil.copy2(source, baseline)

    pixels = np.frombuffer(raw_before, dtype=np.float16).copy().reshape(RES_U, RES_V, 4)
    rgb_before = pixels[:, :, :3].tobytes()
    values = pixels.astype(np.float32)
    height = values[:, :, 0]
    gradient_u = values[:, :, 1]

    positive_crest = smoothstep(height_start, height_full, height)
    near_crest = np.exp(-np.square(np.abs(gradient_u) / max(gradient_width, 1.0e-6)))
    mask = np.power(np.clip(positive_crest * near_crest, 0.0, 1.0), gamma)
    mask = 0.5 * (mask + mask[:, ::-1])

    pixels[:, :, 3] = mask.astype(np.float16)
    raw_after = pixels.tobytes()
    source.write_bytes(raw_after)

    rgb_after = pixels[:, :, :3].tobytes()
    if rgb_before != rgb_after:
        raise AssertionError(f"R/G/B regression while baking {source.name}")

    mask_fp16 = pixels[:, :, 3].astype(np.float32)
    symmetry_max = float(np.max(np.abs(mask_fp16 - mask_fp16[:, ::-1])))
    if symmetry_max > 1.0e-4:
        raise AssertionError(f"Kelvin mask symmetry regression: {source.name} error={symmetry_max}")
    if not np.all(np.isfinite(mask_fp16)):
        raise AssertionError(f"Non-finite Kelvin Foam mask: {source.name}")

    preview_dir.mkdir(parents=True, exist_ok=True)
    preview = np.clip(mask_fp16.T * 255.0, 0.0, 255.0).astype(np.uint8)
    Image.fromarray(preview, mode="L").save(preview_dir / f"{source.stem}_foam_mask.png")

    return {
        "file": source.name,
        "sha256_before": sha256(raw_before),
        "sha256_after": sha256(raw_after),
        "rgb_sha256": sha256(rgb_after),
        "mask_min": float(mask_fp16.min()),
        "mask_mean": float(mask_fp16.mean()),
        "mask_max": float(mask_fp16.max()),
        "coverage_gt_0_05": float(np.mean(mask_fp16 > 0.05)),
        "coverage_gt_0_50": float(np.mean(mask_fp16 > 0.50)),
        "symmetry_max_abs": symmetry_max,
    }


def main() -> None:
    project_dir = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--golden-dir",
        type=Path,
        default=project_dir / "Content/Blueprints/Ship/Data/Kelvin",
    )
    parser.add_argument(
        "--diagnostic-dir",
        type=Path,
        default=project_dir / "Saved/Diagnostics/SWFoam/Kelvin",
    )
    parser.add_argument("--height-start", type=float, default=0.04)
    parser.add_argument("--height-full", type=float, default=0.28)
    parser.add_argument("--gradient-width", type=float, default=1.25)
    parser.add_argument("--gamma", type=float, default=1.0)
    args = parser.parse_args()

    baseline_dir = args.diagnostic_dir / "Baseline"
    preview_dir = args.diagnostic_dir / "GoldenPreviews"
    records = []
    for filename in FILENAMES:
        records.append(
            bake_file(
                args.golden_dir / filename,
                baseline_dir,
                preview_dir,
                args.height_start,
                args.height_full,
                args.gradient_width,
                args.gamma,
            )
        )

    report = {
        "version": "SWImprovedFoam_KelvinMask_1",
        "resolution_u": RES_U,
        "resolution_v": RES_V,
        "parameters": {
            "height_start": args.height_start,
            "height_full": args.height_full,
            "gradient_width": args.gradient_width,
            "gamma": args.gamma,
        },
        "profiles": records,
    }
    args.diagnostic_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.diagnostic_dir / "kelvin_foam_bake_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
