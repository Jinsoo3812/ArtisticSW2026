"""
verify_parity_and_physics.py - Automated Parity, Gradient & Physics Verification Suite
Validates:
  1. Height & Analytical Gradient Quadrature Parity (Scipy Quad vs Vectorized Composite GL)
  2. Gradient Exactness Parity (Analytical dZ/du & dZ/dv vs Central Finite Difference)
  3. Physics Validation (Fr=0.50 wavelength & cusp angle)
  4. FP16 / RGBA16F Quantization Parity
"""

import os
import site
import sys

user_site = site.getusersitepackages()
if os.path.exists(user_site) and user_site not in sys.path:
    sys.path.insert(0, user_site)

import numpy as np
import json
from pathlib import Path
from typing import Dict, Any

from baker_core import (
    ReferenceAdaptiveEvaluator,
    GaussLegendreVectorizedEvaluator,
    KelvinDomainConfig,
    KELVIN_CUSP_ANGLE_DEG,
    KELVIN_CUSP_ANGLE_RAD
)


def verify_quadrature_parity(n_samples: int = 50) -> Dict[str, Any]:
    """Tests height and gradient agreement between Scipy Quad and Composite Gauss-Legendre."""
    print("[Verification] 1. Running Quadrature Parity Test (Scipy Quad vs Composite GL) ...")
    eval_ref = ReferenceAdaptiveEvaluator(epsabs=1e-8, epsrel=1e-8)
    eval_gl = GaussLegendreVectorizedEvaluator(n_sub=8, n_nodes_per_sub=128)

    np.random.seed(42)
    test_u = np.random.uniform(0.5, 8.5, n_samples)
    test_v = np.random.uniform(-2.2, 2.2, n_samples)
    test_fr = np.random.choice([0.30, 0.50, 0.70, 1.00], n_samples)

    diffs_z = []
    diffs_du = []
    diffs_dv = []

    for u, v, fr in zip(test_u, test_v, test_fr):
        z_ref = eval_ref.evaluate_height(float(u), float(v), float(fr))
        du_ref, dv_ref = eval_ref.evaluate_gradients(float(u), float(v), float(fr))

        u_arr = np.array([[u]])
        v_arr = np.array([[v]])
        z_gl, du_gl, dv_gl = eval_gl.evaluate_all(u_arr, v_arr, float(fr))

        diffs_z.append(abs(z_ref - float(z_gl[0, 0])))
        diffs_du.append(abs(du_ref - float(du_gl[0, 0])))
        diffs_dv.append(abs(dv_ref - float(dv_gl[0, 0])))

    max_diff_z = float(np.max(diffs_z))
    max_diff_du = float(np.max(diffs_du))
    max_diff_dv = float(np.max(diffs_dv))
    passed = max_diff_z < 1e-5 and max_diff_du < 1e-4 and max_diff_dv < 1e-4

    print(f"  -> Max Error: Height={max_diff_z:.2e}, GradU={max_diff_du:.2e}, GradV={max_diff_dv:.2e} | Passed: {passed}")
    return {
        "TestName": "Quadrature Parity (Height & Analytical Gradients)",
        "NumSamples": n_samples,
        "MaxAbsoluteError_Height": max_diff_z,
        "MaxAbsoluteError_GradU": max_diff_du,
        "MaxAbsoluteError_GradV": max_diff_dv,
        "Passed": passed
    }


def verify_gradient_finite_difference_parity() -> Dict[str, Any]:
    """Validates analytical gradients against high-order finite differences."""
    print("[Verification] 2. Running Analytical Gradient vs Finite Difference Parity Test ...")
    eval_gl = GaussLegendreVectorizedEvaluator(n_sub=8, n_nodes_per_sub=128)
    
    # Generate fine grid
    u = np.linspace(1.0, 7.0, 100)
    v = np.linspace(-2.0, 2.0, 80)
    U, V = np.meshgrid(u, v, indexing='ij')
    
    Fr = 0.50
    Z, dZu_analytic, dZv_analytic = eval_gl.evaluate_all(U, V, Fr)
    
    du = u[1] - u[0]
    dv = v[1] - v[0]
    
    # 2nd-order Central difference
    dZu_num = np.gradient(Z, du, axis=0)
    dZv_num = np.gradient(Z, dv, axis=1)
    
    # Compare inside the domain (away from edges)
    diff_u = np.abs(dZu_analytic[5:-5, 5:-5] - dZu_num[5:-5, 5:-5])
    diff_v = np.abs(dZv_analytic[5:-5, 5:-5] - dZv_num[5:-5, 5:-5])
    
    max_err_u = float(np.max(diff_u))
    max_err_v = float(np.max(diff_v))
    mean_err_u = float(np.mean(diff_u))
    mean_err_v = float(np.mean(diff_v))
    
    passed = mean_err_u < 0.08 and mean_err_v < 0.08
    print(f"  -> Analytical vs NumDiff Mean Error: GradU={mean_err_u:.4e}, GradV={mean_err_v:.4e} | Passed: {passed}")
    
    return {
        "TestName": "Analytical Gradient vs Numerical Finite Difference",
        "MeanError_GradU": mean_err_u,
        "MeanError_GradV": mean_err_v,
        "MaxError_GradU": max_err_u,
        "MaxError_GradV": max_err_v,
        "Tolerance": 0.08,
        "Passed": passed
    }


def verify_fr050_physics() -> Dict[str, Any]:
    """Validates physical properties for Fr=0.50 golden slice."""
    print("[Verification] 3. Running Fr=0.50 Physics Sanity Checks ...")
    eval_gl = GaussLegendreVectorizedEvaluator(n_sub=8, n_nodes_per_sub=128)

    u_fine = np.linspace(1.0, 9.0, 800)
    v_zero = np.zeros_like(u_fine)
    z_center = eval_gl.evaluate_grid(u_fine[:, None], v_zero[:, None], 0.50)[:, 0]

    peaks = []
    for i in range(1, len(z_center) - 1):
        if z_center[i] > z_center[i-1] and z_center[i] > z_center[i+1] and z_center[i] > 0.05:
            peaks.append(u_fine[i])

    peak_diffs = np.diff(peaks)
    mean_wavelength = float(np.mean(peak_diffs)) if len(peak_diffs) > 0 else 0.0
    wavelength_error = abs(mean_wavelength - 1.0)
    wavelength_passed = wavelength_error < 0.03

    u_pts = np.linspace(2.0, 8.0, 20)
    inside_amp = []
    outside_amp = []
    for u in u_pts:
        v_cusp = u * np.tan(KELVIN_CUSP_ANGLE_RAD)
        z_in = abs(eval_gl.evaluate_grid(np.array([[u]]), np.array([[v_cusp * 0.5]]), 0.50)[0, 0])
        z_out = abs(eval_gl.evaluate_grid(np.array([[u]]), np.array([[v_cusp * 1.6 + 0.3]]), 0.50)[0, 0])
        inside_amp.append(z_in)
        outside_amp.append(z_out)

    mean_inside = float(np.mean(inside_amp))
    mean_outside = float(np.mean(outside_amp))
    decay_ratio = mean_outside / max(mean_inside, 1e-6)
    decay_passed = decay_ratio < 0.05

    print(f"  -> Centerline Wavelength: {mean_wavelength:.3f} lambda (Target: 1.00 lambda) | Passed: {wavelength_passed}")
    print(f"  -> Outer Cusp Decay Ratio: {decay_ratio:.4f} | Passed: {decay_passed}")

    return {
        "TestName": "Fr=0.50 Physics & Envelope Checks",
        "MeasuredCenterlineWavelength": mean_wavelength,
        "TargetWavelength": 1.0,
        "WavelengthError": wavelength_error,
        "DecayRatioOutsideCusp": decay_ratio,
        "OverallPassed": wavelength_passed and decay_passed
    }


def verify_fp16_quantization(atlas_dir: Path) -> Dict[str, Any]:
    """Validates quantization error for RGBA16F atlas payload."""
    print("[Verification] 4. Running RGBA16F Quantization Error Analysis ...")
    meta_path = atlas_dir / "kelvin_wake_atlas_meta.json"
    bin_path = atlas_dir / "kelvin_wake_atlas_gradient_fp16.bin"

    if not meta_path.exists() or not bin_path.exists():
        return {"TestName": "RGBA16F Quantization", "Passed": False, "Error": "Atlas files not found"}

    with open(meta_path, "r", encoding="utf-8") as f:
        meta = json.load(f)

    n_slices = meta["NumSlices"]
    res_u = meta["ResolutionU"]
    res_v = meta["ResolutionV"]

    with open(bin_path, "rb") as f:
        raw = f.read()
    atlas_rgba_fp16 = np.frombuffer(raw, dtype=np.float16).reshape((n_slices, res_u, res_v, 4))

    evaluator = GaussLegendreVectorizedEvaluator(n_sub=8, n_nodes_per_sub=128)
    domain_cfg = KelvinDomainConfig(
        u_min=meta["Domain"]["UMin"],
        u_max=meta["Domain"]["UMax"],
        v_min=meta["Domain"]["VMin"],
        v_max=meta["Domain"]["VMax"],
        res_u=res_u,
        res_v=res_v
    )
    U, V = domain_cfg.get_coordinate_grids()

    max_errors_z = []
    for i, Fr in enumerate(meta["FroudeSlices"]):
        z_f64, _, _ = evaluator.evaluate_all(U, V, Fr)
        z_fp16_norm = atlas_rgba_fp16[i, :, :, 0].astype(np.float64)
        z_abs_max = meta["SliceMaxAmplitudes"][i]
        z_fp16 = z_fp16_norm * z_abs_max

        diff = np.abs(z_f64 - z_fp16)
        max_errors_z.append(float(np.max(diff)))

    global_max_err = float(np.max(max_errors_z))
    passed = global_max_err < 0.01

    print(f"  -> Global Max RGBA16F Reconstructed Error: {global_max_err:.5f} | Passed: {passed}")
    return {
        "TestName": "RGBA16F Quantization Parity",
        "GlobalMaxAbsoluteError": global_max_err,
        "Passed": passed
    }


def run_all_verifications(atlas_dir_str: str = "atlas_output", output_dir_str: str = "."):
    """Runs complete verification pipeline and writes summary reports."""
    atlas_dir = Path(atlas_dir_str)
    out_dir = Path(output_dir_str)

    res_quad = verify_quadrature_parity()
    res_diff = verify_gradient_finite_difference_parity()
    res_phys = verify_fr050_physics()
    res_fp16 = verify_fp16_quantization(atlas_dir)

    all_passed = res_quad["Passed"] and res_diff["Passed"] and res_phys["OverallPassed"] and res_fp16["Passed"]

    report = {
        "AllPassed": all_passed,
        "Results": {
            "QuadratureParity": res_quad,
            "GradientExactness": res_diff,
            "Fr050Physics": res_phys,
            "FP16Quantization": res_fp16
        }
    }

    json_path = out_dir / "VALIDATION_REPORT.json"
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    print(f"\n[Verification] Full JSON Report saved to {json_path}")

    md_content = f"""# Kelvin Wake Analytical Gradient Validation Summary

**Overall Status**: {"✅ ALL TESTS PASSED (100%)" if all_passed else "❌ TESTS FAILED"}

---

## 1. Quadrature & Gradient Parity
- **Status**: {"✅ PASSED" if res_quad["Passed"] else "❌ FAILED"}
- **Height Max Error**: `{res_quad["MaxAbsoluteError_Height"]:.2e}`
- **GradU Max Error**: `{res_quad["MaxAbsoluteError_GradU"]:.2e}`
- **GradV Max Error**: `{res_quad["MaxAbsoluteError_GradV"]:.2e}`

## 2. Gradient Exactness (Analytical vs Numerical)
- **Status**: {"✅ PASSED" if res_diff["Passed"] else "❌ FAILED"}
- **Mean Error GradU**: `{res_diff["MeanError_GradU"]:.4e}`
- **Mean Error GradV**: `{res_diff["MeanError_GradV"]:.4e}`

## 3. Physics & Envelope Checks ($Fr = 0.50$)
- **Status**: {"✅ PASSED" if res_phys["OverallPassed"] else "❌ FAILED"}
- **Centerline Transverse Wavelength**: `{res_phys["MeasuredCenterlineWavelength"]:.4f} λ` (Target: `1.0000 λ`)
- **Outer Cusp Decay Ratio**: `{res_phys["DecayRatioOutsideCusp"]:.4f}`

## 4. RGBA16F Quantization Parity
- **Status**: {"✅ PASSED" if res_fp16["Passed"] else "❌ FAILED"}
- **Global Max Reconstructed Error**: `{res_fp16["GlobalMaxAbsoluteError"]:.5f}`
"""
    md_path = out_dir / "VALIDATION_SUMMARY.md"
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(md_content)
    print(f"[Verification] Markdown Summary saved to {md_path}\n")


if __name__ == "__main__":
    current_dir = Path(__file__).parent
    atlas_dir = current_dir / "atlas_output"
    run_all_verifications(str(atlas_dir), str(current_dir))
