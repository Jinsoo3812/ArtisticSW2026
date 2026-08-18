import os
import glob
import numpy as np

# Detailed comparison script between V3 and V4
from analyze_v4_data import parse_csv_file, parse_profile_gpu_file

v3_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V3_Kelvin_Capacity_and_CPU_Copy"
v4_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V4_Ripple_pixel_gradient"

v3_csvs = [parse_csv_file(glob.glob(os.path.join(s, "*.csv"))[0]) for s in sorted(glob.glob(os.path.join(v3_dir, "*"))) if os.path.isdir(s)]
v4_csvs = [parse_csv_file(glob.glob(os.path.join(s, "*.csv"))[0]) for s in sorted(glob.glob(os.path.join(v4_dir, "*"))) if os.path.isdir(s)]

v3_logs = [parse_profile_gpu_file(glob.glob(os.path.join(s, "*.[tT][xX][tT]"))[0]) for s in sorted(glob.glob(os.path.join(v3_dir, "*"))) if os.path.isdir(s)]
v3_logs = [l for l in v3_logs if len(l) > 1]
v4_logs = [parse_profile_gpu_file(glob.glob(os.path.join(s, "*.[tT][xX][tT]"))[0]) for s in sorted(glob.glob(os.path.join(v4_dir, "*"))) if os.path.isdir(s)]

print("="*85)
print(f"{'Metric':<35} | {'V3 Mean':<12} | {'V4 (All Mean)':<15} | {'V4 (Run1 Pure)':<15}")
print("="*85)

for k in ["FrameTime_Mean", "FPS_Mean", "GPUTime", "GameThreadTime", "RHI/DrawCalls", "RHI/PrimitivesDrawn", "Exclusive/GameThread/EventWait"]:
    v3_m = np.mean([c[k] for c in v3_csvs if k in c])
    v4_m = np.mean([c[k] for c in v4_csvs if k in c])
    v4_run1 = v4_csvs[0].get(k, 0.0)
    print(f"{k:<35} | {v3_m:<12.3f} | {v4_m:<15.3f} | {v4_run1:<15.3f}")

print("\n" + "="*85)
print("=== ProfileGPU LOG METRICS (GPU Pure Render Time) ===")
print(f"{'GPU Event':<35} | {'V3 Mean':<12} | {'V4 Mean':<15} | {'Delta':<12} | {'Rate (%)':<10}")
print("="*85)

keys = ["SingleLayerWater", "SLW::Draw", "SLW::LumenReflections", "SLW_DepthPrepass", "ShadowDepths", "VSM_Nanite", "VSM_NonNanite", "PostProcessing", "TSR", "VolumetricCloud", "RenderDeferredLighting", "LumenSceneLighting"]

for k in keys:
    v3_m = np.mean([l[k] for l in v3_logs if k in l])
    v4_m = np.mean([l[k] for l in v4_logs if k in l])
    delta = v4_m - v3_m
    rate = (delta / v3_m) * 100.0 if v3_m != 0 else 0.0
    print(f"{k:<35} | {v3_m:<12.3f} | {v4_m:<15.3f} | {delta:<+12.3f} | {rate:<+10.2f}%")
