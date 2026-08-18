"""
generate_golden_images.py - Golden Reference Image & Gradient Dataset Generator
Generates high-resolution reference wave patterns with analytical gradients (R=Height, G=dZ/du, B=dZ/dv) for:
  Fr = 0.30, 0.50, 0.70, 1.00

Outputs saved in ./golden_images/:
  - 2D signed-height colormaps with Kelvin cusp (19.47 deg) overlay
  - 2D analytical gradient maps (dZ/du downstream slope, dZ/dv lateral slope)
  - 2D composite RGB normal / gradient maps
  - 3D perspective surface renderings
  - Centerline and transverse cross-sectional profiles (Elevation & Slope)
  - Angle-resolved amplitude spectrum (Darmon et al. 2013 Fig 2/3 replication)
  - Raw numpy .npy float32 data matrices (Height & 3-channel [Z, dZu, dZv])
  - Packed RGBA16F / FP16 binary payloads for Unreal Engine
  - Comparative summary montages
"""

import os
import site
import sys

user_site = site.getusersitepackages()
if os.path.exists(user_site) and user_site not in sys.path:
    sys.path.insert(0, user_site)

import numpy as np
import matplotlib
matplotlib.use('Agg')  # Headless rendering
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from matplotlib.colors import TwoSlopeNorm
import json
import struct
from pathlib import Path
from typing import Tuple, List, Dict, Any

from baker_core import (
    GaussLegendreVectorizedEvaluator,
    KelvinDomainConfig,
    KELVIN_CUSP_ANGLE_DEG,
    KELVIN_CUSP_ANGLE_RAD
)


def compute_angle_elevation_profile(
    Z: np.ndarray,
    u_lin: np.ndarray,
    v_lin: np.ndarray,
    max_phi_deg: float = 45.0,
    n_phi_samples: int = 120,
    min_radius: float = 1.0,
    max_radius: float = 9.0,
    n_radial_samples: int = 200
) -> Tuple[np.ndarray, np.ndarray]:
    """Computes maximum wave amplitude along radial rays at angle phi to the downstream axis u."""
    from scipy.interpolate import RegularGridInterpolator
    
    interp = RegularGridInterpolator((u_lin, v_lin), Z, bounds_error=False, fill_value=0.0)
    phi_deg_arr = np.linspace(0.5, max_phi_deg, n_phi_samples)
    peak_elevations = np.zeros(n_phi_samples)
    radii = np.linspace(min_radius, max_radius, n_radial_samples)
    
    for i, phi_deg in enumerate(phi_deg_arr):
        phi_rad = np.radians(phi_deg)
        u_ray = radii * np.cos(phi_rad)
        v_ray = radii * np.sin(phi_rad)
        points = np.column_stack((u_ray, v_ray))
        z_ray = np.abs(interp(points))
        peak_elevations[i] = np.percentile(z_ray, 95) if len(z_ray) > 0 else 0.0
        
    max_val = np.max(peak_elevations)
    if max_val > 1e-9:
        peak_elevations /= max_val
        
    return phi_deg_arr, peak_elevations


def generate_golden_slice(
    Fr: float,
    evaluator: GaussLegendreVectorizedEvaluator,
    domain_cfg: KelvinDomainConfig,
    output_dir: Path
) -> dict:
    """Generates all golden visualizations, gradient maps, and binary data for a given Froude number."""
    print(f"\n[Golden Generator] Processing Fr = {Fr:.2f} (Height + Analytical Gradients) ...")
    
    U, V = domain_cfg.get_coordinate_grids()
    u_lin = np.linspace(domain_cfg.u_min, domain_cfg.u_max, domain_cfg.res_u)
    v_lin = np.linspace(domain_cfg.v_min, domain_cfg.v_max, domain_cfg.res_v)
    
    # 1. Compute high-precision elevation and analytical gradients simultaneously
    Z, dZu, dZv = evaluator.evaluate_all(U, V, Fr)
    
    z_min, z_max = float(np.min(Z)), float(np.max(Z))
    z_abs_max = max(abs(z_min), abs(z_max), 1e-6)
    
    dzu_min, dzu_max = float(np.min(dZu)), float(np.max(dZu))
    dzu_abs_max = max(abs(dzu_min), abs(dzu_max), 1e-6)
    
    dzv_min, dzv_max = float(np.min(dZv)), float(np.max(dZv))
    dzv_abs_max = max(abs(dzv_min), abs(dzv_max), 1e-6)
    
    # 2. Save Raw numpy arrays
    # 2-A: Single Height array (shape: [512, 256])
    npy_z_path = output_dir / f"golden_Fr{Fr:.2f}_elevation.npy"
    np.save(npy_z_path, Z.astype(np.float32))
    
    # 2-B: 3-Channel combined array [Z, dZu, dZv] (shape: [512, 256, 3])
    stacked_f32 = np.stack([Z, dZu, dZv], axis=-1).astype(np.float32)
    npy_grad_path = output_dir / f"golden_Fr{Fr:.2f}_data_gradient.npy"
    np.save(npy_grad_path, stacked_f32)
    
    # 2-C: Normalized Packed RGBA16F Binary Payload for Unreal Engine
    # R = Z / z_abs_max
    # G = dZu / z_abs_max
    # B = dZv / z_abs_max
    # A = 1.0 (mask)
    inv_peak = 1.0 / z_abs_max
    norm_Z = (Z * inv_peak).astype(np.float16)
    norm_dZu = (dZu * inv_peak).astype(np.float16)
    norm_dZv = (dZv * inv_peak).astype(np.float16)
    norm_A = np.ones_like(norm_Z, dtype=np.float16)
    
    # Shape: (512, 256, 4) in row-major order (U-major, V-minor)
    rgba_fp16 = np.stack([norm_Z, norm_dZu, norm_dZv, norm_A], axis=-1)
    
    fr_tag = f"fr{int(round(Fr * 100)):03d}"
    bin_path = output_dir / f"kelvin_wake_golden_{fr_tag}_rgba16f.bin"
    with open(bin_path, "wb") as f:
        f.write(rgba_fp16.tobytes())
        
    # Also save standard R16F single channel for backwards-compatibility
    bin_r16f_path = output_dir / f"kelvin_wake_golden_{fr_tag}_fp16.bin"
    with open(bin_r16f_path, "wb") as f:
        f.write(norm_Z.tobytes())
    
    # 3. Visual Plots
    # 3-A: 2D Signed Height Colormap
    fig, ax = plt.subplots(figsize=(10, 6), dpi=200)
    norm = TwoSlopeNorm(vmin=-z_abs_max, vcenter=0.0, vmax=z_abs_max)
    im = ax.imshow(
        Z.T, extent=[domain_cfg.u_min, domain_cfg.u_max, domain_cfg.v_min, domain_cfg.v_max],
        origin='lower', cmap='RdBu_r', norm=norm, aspect='equal', interpolation='bicubic'
    )
    u_cusp = np.linspace(0, domain_cfg.u_max, 200)
    v_cusp_pos = u_cusp * np.tan(KELVIN_CUSP_ANGLE_RAD)
    ax.plot(u_cusp, v_cusp_pos, 'k--', linewidth=1.5, alpha=0.85, label='Kelvin Cusp (19.47°)')
    ax.plot(u_cusp, -v_cusp_pos, 'k--', linewidth=1.5, alpha=0.85)
    ax.plot(0, 0, 'ro', markersize=7, markeredgecolor='white', label='Pressure Apex (u=0, v=0)')
    ax.set_xlim(domain_cfg.u_min, domain_cfg.u_max)
    ax.set_ylim(domain_cfg.v_min, domain_cfg.v_max)
    ax.set_xlabel(r'Downstream Distance $u = x / \lambda$', fontsize=12)
    ax.set_ylabel(r'Lateral Distance $v = y / \lambda$', fontsize=12)
    ax.set_title(f'Kelvin Wake Golden Elevation Field (Fr = {Fr:.2f}) [R Channel]\nPeak Amplitude $Z_{{peak}} = {z_abs_max:.4f}$', fontsize=13, fontweight='bold')
    ax.legend(loc='upper right', framealpha=0.9)
    ax.grid(True, linestyle=':', alpha=0.5)
    cbar = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.03)
    cbar.set_label(r'Signed Height $\zeta(u, v)$', fontsize=11)
    fig_2d_path = output_dir / f"golden_Fr{Fr:.2f}_2D_height.png"
    plt.tight_layout()
    plt.savefig(fig_2d_path)
    plt.close(fig)
    
    # 3-B: 2D Downstream Gradient (dZ/du) [G Channel]
    fig, ax = plt.subplots(figsize=(10, 6), dpi=200)
    norm_u = TwoSlopeNorm(vmin=-dzu_abs_max, vcenter=0.0, vmax=dzu_abs_max)
    im = ax.imshow(
        dZu.T, extent=[domain_cfg.u_min, domain_cfg.u_max, domain_cfg.v_min, domain_cfg.v_max],
        origin='lower', cmap='PuOr', norm=norm_u, aspect='equal', interpolation='bicubic'
    )
    ax.plot(u_cusp, v_cusp_pos, 'k--', linewidth=1.2, alpha=0.7)
    ax.plot(u_cusp, -v_cusp_pos, 'k--', linewidth=1.2, alpha=0.7)
    ax.set_xlim(domain_cfg.u_min, domain_cfg.u_max)
    ax.set_ylim(domain_cfg.v_min, domain_cfg.v_max)
    ax.set_xlabel(r'Downstream Distance $u = x / \lambda$', fontsize=12)
    ax.set_ylabel(r'Lateral Distance $v = y / \lambda$', fontsize=12)
    ax.set_title(f'Analytical Downstream Gradient $\partial\zeta/\partial u$ (Fr = {Fr:.2f}) [G Channel]\nPeak Slope = {dzu_abs_max:.4f}', fontsize=13, fontweight='bold')
    ax.grid(True, linestyle=':', alpha=0.5)
    cbar = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.03)
    cbar.set_label(r'Slope $\partial\zeta/\partial u$', fontsize=11)
    fig_grad_u_path = output_dir / f"golden_Fr{Fr:.2f}_2D_gradient_u.png"
    plt.tight_layout()
    plt.savefig(fig_grad_u_path)
    plt.close(fig)
    
    # 3-C: 2D Lateral Gradient (dZ/dv) [B Channel]
    fig, ax = plt.subplots(figsize=(10, 6), dpi=200)
    norm_v = TwoSlopeNorm(vmin=-dzv_abs_max, vcenter=0.0, vmax=dzv_abs_max)
    im = ax.imshow(
        dZv.T, extent=[domain_cfg.u_min, domain_cfg.u_max, domain_cfg.v_min, domain_cfg.v_max],
        origin='lower', cmap='PRGn', norm=norm_v, aspect='equal', interpolation='bicubic'
    )
    ax.plot(u_cusp, v_cusp_pos, 'k--', linewidth=1.2, alpha=0.7)
    ax.plot(u_cusp, -v_cusp_pos, 'k--', linewidth=1.2, alpha=0.7)
    ax.set_xlim(domain_cfg.u_min, domain_cfg.u_max)
    ax.set_ylim(domain_cfg.v_min, domain_cfg.v_max)
    ax.set_xlabel(r'Downstream Distance $u = x / \lambda$', fontsize=12)
    ax.set_ylabel(r'Lateral Distance $v = y / \lambda$', fontsize=12)
    ax.set_title(f'Analytical Lateral Gradient $\partial\zeta/\partial v$ (Fr = {Fr:.2f}) [B Channel]\nPeak Slope = {dzv_abs_max:.4f}', fontsize=13, fontweight='bold')
    ax.grid(True, linestyle=':', alpha=0.5)
    cbar = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.03)
    cbar.set_label(r'Slope $\partial\zeta/\partial v$', fontsize=11)
    fig_grad_v_path = output_dir / f"golden_Fr{Fr:.2f}_2D_gradient_v.png"
    plt.tight_layout()
    plt.savefig(fig_grad_v_path)
    plt.close(fig)
    
    # 3-D: RGB Composite Normal Map Visualization (Normal = normalize(-dZu, -dZv, 1.0))
    fig, ax = plt.subplots(figsize=(10, 6), dpi=200)
    # Map normal vector [-1, 1] to RGB [0, 1]
    norm_len = np.sqrt(dZu**2 + dZv**2 + 1.0)
    normal_x = (-dZu / norm_len) * 0.5 + 0.5
    normal_y = (-dZv / norm_len) * 0.5 + 0.5
    normal_z = (1.0 / norm_len) * 0.5 + 0.5
    normal_rgb = np.stack([normal_x, normal_y, normal_z], axis=-1)
    
    ax.imshow(
        np.transpose(normal_rgb, (1, 0, 2)),
        extent=[domain_cfg.u_min, domain_cfg.u_max, domain_cfg.v_min, domain_cfg.v_max],
        origin='lower', aspect='equal', interpolation='bicubic'
    )
    ax.plot(u_cusp, v_cusp_pos, 'w--', linewidth=1.2, alpha=0.8)
    ax.plot(u_cusp, -v_cusp_pos, 'w--', linewidth=1.2, alpha=0.8)
    ax.set_xlim(domain_cfg.u_min, domain_cfg.u_max)
    ax.set_ylim(domain_cfg.v_min, domain_cfg.v_max)
    ax.set_xlabel(r'Downstream Distance $u = x / \lambda$', fontsize=12)
    ax.set_ylabel(r'Lateral Distance $v = y / \lambda$', fontsize=12)
    ax.set_title(f'Analytical Tangent-Space Surface Normal Map (Fr = {Fr:.2f})\nBaked Direct Normal: $\\mathbf{{N}} = \\text{{normalize}}(-\\partial\\zeta/\\partial u, -\\partial\\zeta/\\partial v, 1)$', fontsize=13, fontweight='bold')
    ax.grid(True, linestyle=':', alpha=0.3, color='white')
    fig_normal_path = output_dir / f"golden_Fr{Fr:.2f}_2D_gradient_normal_rgb.png"
    plt.tight_layout()
    plt.savefig(fig_normal_path)
    plt.close(fig)
    
    # 3-E: 3D Surface Plot
    fig = plt.figure(figsize=(11, 7), dpi=200)
    ax = fig.add_subplot(111, projection='3d')
    step_u = max(1, domain_cfg.res_u // 128)
    step_v = max(1, domain_cfg.res_v // 128)
    surf = ax.plot_surface(
        U[::step_u, ::step_v], V[::step_u, ::step_v], Z[::step_u, ::step_v],
        cmap='coolwarm', norm=norm, edgecolor='none', antialiased=True, alpha=0.92
    )
    ax.set_xlabel(r'Downstream $u / \lambda$', fontsize=10, labelpad=8)
    ax.set_ylabel(r'Lateral $v / \lambda$', fontsize=10, labelpad=8)
    ax.set_zlabel(r'Height $\zeta$', fontsize=10, labelpad=8)
    ax.set_title(f'3D Kelvin Wake Surface Elevation (Fr = {Fr:.2f})', fontsize=13, fontweight='bold')
    ax.view_init(elev=32, azim=-125)
    cbar = fig.colorbar(surf, ax=ax, shrink=0.55, aspect=12, pad=0.1)
    cbar.set_label(r'Height $\zeta$', fontsize=10)
    fig_3d_path = output_dir / f"golden_Fr{Fr:.2f}_3D_surface.png"
    plt.tight_layout()
    plt.savefig(fig_3d_path)
    plt.close(fig)
    
    # 3-F: Profiles (Height vs Downstream Slope)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), dpi=200)
    center_idx = len(v_lin) // 2
    ax1.plot(u_lin, Z[:, center_idx], 'b-', linewidth=2.0, label=r'Centerline Elevation $\zeta(u, 0)$')
    ax1.plot(u_lin, dZu[:, center_idx], 'r--', linewidth=1.5, label=r'Centerline Slope $\partial\zeta/\partial u(u, 0)$')
    ax1.axhline(0, color='gray', linestyle='--', alpha=0.6)
    ax1.set_xlim(domain_cfg.u_min, domain_cfg.u_max)
    ax1.set_xlabel(r'Downstream Distance $u = x / \lambda$', fontsize=11)
    ax1.set_ylabel(r'Amplitude', fontsize=11)
    ax1.set_title(f'Centerline Wave & Analytical Slope Profile (Fr = {Fr:.2f})', fontsize=12, fontweight='bold')
    ax1.legend(loc='upper right')
    ax1.grid(True, linestyle=':', alpha=0.6)
    
    stations = [1.0, 2.0, 5.0, 8.0]
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']
    for u_stat, col in zip(stations, colors):
        u_idx = int(np.clip(u_stat / domain_cfg.u_max * (len(u_lin) - 1), 0, len(u_lin) - 1))
        ax2.plot(v_lin, Z[u_idx, :], label=f'u = {u_stat:.1f} $\lambda$', color=col, linewidth=1.8)
        v_cusp = u_stat * np.tan(KELVIN_CUSP_ANGLE_RAD)
        ax2.axvline(v_cusp, color=col, linestyle=':', alpha=0.4)
        ax2.axvline(-v_cusp, color=col, linestyle=':', alpha=0.4)
        
    ax2.axhline(0, color='gray', linestyle='--', alpha=0.6)
    ax2.set_xlim(domain_cfg.v_min, domain_cfg.v_max)
    ax2.set_xlabel(r'Lateral Distance $v = y / \lambda$', fontsize=11)
    ax2.set_ylabel(r'Elevation $\zeta$', fontsize=11)
    ax2.set_title(f'Lateral Wave Profiles across Wake (Fr = {Fr:.2f})', fontsize=12, fontweight='bold')
    ax2.legend(loc='upper right')
    ax2.grid(True, linestyle=':', alpha=0.6)
    fig_profiles_path = output_dir / f"golden_Fr{Fr:.2f}_profiles.png"
    plt.tight_layout()
    plt.savefig(fig_profiles_path)
    plt.close(fig)
    
    # 3-G: Angle Spectrum
    phi_arr, peak_arr = compute_angle_elevation_profile(Z, u_lin, v_lin)
    peak_phi_idx = np.argmax(peak_arr)
    apparent_angle = phi_arr[peak_phi_idx]
    fig, ax = plt.subplots(figsize=(9, 5), dpi=200)
    ax.plot(phi_arr, peak_arr, 'b-', linewidth=2.2, label=r'Normalized Max Elevation $Z(\phi) / Z_{max}$')
    ax.axvline(KELVIN_CUSP_ANGLE_DEG, color='red', linestyle='--', linewidth=1.8, label=f'Kelvin Cusp ({KELVIN_CUSP_ANGLE_DEG:.2f}°)')
    ax.axvline(apparent_angle, color='green', linestyle=':', linewidth=1.8, label=f'Apparent Peak ({apparent_angle:.2f}°)')
    ax.set_xlim(0, 45)
    ax.set_ylim(0, 1.08)
    ax.set_xlabel(r'Angle $\phi$ [degrees]', fontsize=11)
    ax.set_ylabel(r'Relative Amplitude', fontsize=11)
    ax.set_title(f'Wave Amplitude vs Wake Angle $\phi$ (Fr = {Fr:.2f})\nVerification of Darmon et al. (2013)', fontsize=12, fontweight='bold')
    ax.legend(loc='upper right')
    ax.grid(True, linestyle=':', alpha=0.6)
    fig_angle_path = output_dir / f"golden_Fr{Fr:.2f}_angle_spectrum.png"
    plt.tight_layout()
    plt.savefig(fig_angle_path)
    plt.close(fig)
    
    slice_info = {
        "FroudeNumber": Fr,
        "Z_Min": z_min,
        "Z_Max": z_max,
        "Z_AbsMax": z_abs_max,
        "GradU_Min": dzu_min,
        "GradU_Max": dzu_max,
        "GradU_AbsMax": dzu_abs_max,
        "GradV_Min": dzv_min,
        "GradV_Max": dzv_max,
        "GradV_AbsMax": dzv_abs_max,
        "ApparentPeakAngleDeg": float(apparent_angle),
        "KelvinCuspAngleDeg": KELVIN_CUSP_ANGLE_DEG,
        "Files": {
            "NpyDataGradient": npy_grad_path.name,
            "NpyElevation": npy_z_path.name,
            "BinaryRGBA16F": bin_path.name,
            "BinaryFP16": bin_r16f_path.name,
            "2DColormap": fig_2d_path.name,
            "2DGradientU": fig_grad_u_path.name,
            "2DGradientV": fig_grad_v_path.name,
            "2DNormalRGB": fig_normal_path.name,
            "3DSurface": fig_3d_path.name,
            "Profiles": fig_profiles_path.name,
            "AngleSpectrum": fig_angle_path.name
        }
    }
    return slice_info


def generate_all_golden_images(
    golden_froude_list: List[float] = [0.30, 0.50, 0.70, 1.00],
    output_dir_str: str = "golden_images"
):
    """Executes full golden suite with gradients and creates comparative summary montages."""
    output_dir = Path(output_dir_str)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    evaluator = GaussLegendreVectorizedEvaluator(n_nodes=256)
    domain_cfg = KelvinDomainConfig(
        u_min=0.0,
        u_max=10.0,
        v_min=-3.0,
        v_max=+3.0,
        res_u=512,
        res_v=256
    )
    
    all_slice_info = []
    for Fr in golden_froude_list:
        info = generate_golden_slice(Fr, evaluator, domain_cfg, output_dir)
        all_slice_info.append(info)
        
    meta_path = output_dir / "golden_slices_metadata.json"
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(all_slice_info, f, indent=2)
    print(f"\n[Golden Generator] Metadata saved to {meta_path}")
    
    # 4-Panel Elevation Comparison Montage
    print("[Golden Generator] Generating 4-Panel Comparative Elevation Montage ...")
    fig, axes = plt.subplots(2, 2, figsize=(16, 12), dpi=200)
    for i, (Fr, ax) in enumerate(zip(golden_froude_list, axes.ravel())):
        npy_file = output_dir / f"golden_Fr{Fr:.2f}_elevation.npy"
        Z = np.load(npy_file)
        z_abs = max(abs(np.min(Z)), abs(np.max(Z)), 1e-6)
        norm = TwoSlopeNorm(vmin=-z_abs, vcenter=0.0, vmax=z_abs)
        im = ax.imshow(
            Z.T, extent=[domain_cfg.u_min, domain_cfg.u_max, domain_cfg.v_min, domain_cfg.v_max],
            origin='lower', cmap='RdBu_r', norm=norm, aspect='equal', interpolation='bicubic'
        )
        u_cusp = np.linspace(0, domain_cfg.u_max, 100)
        v_cusp = u_cusp * np.tan(KELVIN_CUSP_ANGLE_RAD)
        ax.plot(u_cusp, v_cusp, 'k--', linewidth=1.2, alpha=0.8)
        ax.plot(u_cusp, -v_cusp, 'k--', linewidth=1.2, alpha=0.8)
        ax.plot(0, 0, 'ro', markersize=5)
        app_ang = all_slice_info[i]["ApparentPeakAngleDeg"]
        ax.set_title(f'Fr = {Fr:.2f} | Peak Angle = {app_ang:.1f}° | Peak Z = {z_abs:.4f}', fontsize=12, fontweight='bold')
        ax.set_xlabel(r'$u = x / \lambda$', fontsize=10)
        ax.set_ylabel(r'$v = y / \lambda$', fontsize=10)
        ax.grid(True, linestyle=':', alpha=0.4)
        cbar = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.03)
        cbar.set_label(r'$\zeta$', fontsize=9)
        
    plt.suptitle("Kelvin Wake Elevation Fields (R Channel) across Froude Numbers", fontsize=14, fontweight='bold', y=0.98)
    plt.tight_layout()
    montage_elev_path = output_dir / "golden_comparison_montage.png"
    plt.savefig(montage_elev_path)
    plt.close(fig)
    
    # 4-Panel Normal RGB Map Comparison Montage
    print("[Golden Generator] Generating 4-Panel Comparative Normal RGB Montage ...")
    fig, axes = plt.subplots(2, 2, figsize=(16, 12), dpi=200)
    for i, (Fr, ax) in enumerate(zip(golden_froude_list, axes.ravel())):
        npy_grad = output_dir / f"golden_Fr{Fr:.2f}_data_gradient.npy"
        data = np.load(npy_grad) # shape: (512, 256, 3) -> [Z, dZu, dZv]
        dZu = data[:, :, 1]
        dZv = data[:, :, 2]
        norm_len = np.sqrt(dZu**2 + dZv**2 + 1.0)
        rgb = np.stack([(-dZu / norm_len) * 0.5 + 0.5, (-dZv / norm_len) * 0.5 + 0.5, (1.0 / norm_len) * 0.5 + 0.5], axis=-1)
        ax.imshow(
            np.transpose(rgb, (1, 0, 2)),
            extent=[domain_cfg.u_min, domain_cfg.u_max, domain_cfg.v_min, domain_cfg.v_max],
            origin='lower', aspect='equal', interpolation='bicubic'
        )
        u_cusp = np.linspace(0, domain_cfg.u_max, 100)
        v_cusp = u_cusp * np.tan(KELVIN_CUSP_ANGLE_RAD)
        ax.plot(u_cusp, v_cusp, 'w--', linewidth=1.2, alpha=0.8)
        ax.plot(u_cusp, -v_cusp, 'w--', linewidth=1.2, alpha=0.8)
        ax.set_title(f'Fr = {Fr:.2f} Analytical Normal Vector Map', fontsize=12, fontweight='bold')
        ax.set_xlabel(r'$u = x / \lambda$', fontsize=10)
        ax.set_ylabel(r'$v = y / \lambda$', fontsize=10)
        ax.grid(True, linestyle=':', alpha=0.3, color='white')
        
    plt.suptitle("Kelvin Wake Analytical Normal Maps (Baked G=dZ/du, B=dZ/dv -> Direct Normal)", fontsize=14, fontweight='bold', y=0.98)
    plt.tight_layout()
    montage_normal_path = output_dir / "golden_gradient_montage.png"
    plt.savefig(montage_normal_path)
    plt.close(fig)
    
    print(f"[Golden Generator] All golden images and gradient maps generated successfully in {output_dir}!")


if __name__ == "__main__":
    current_dir = Path(__file__).parent
    out_dir = current_dir / "golden_images"
    generate_all_golden_images([0.30, 0.50, 0.70, 1.00], str(out_dir))
