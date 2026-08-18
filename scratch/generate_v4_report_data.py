import os
import glob
import numpy as np

from analyze_v4_data import parse_csv_file, parse_profile_gpu_file

v3_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V3_Kelvin_Capacity_and_CPU_Copy"
v4_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V4_Ripple_pixel_gradient"

v3_csvs = [parse_csv_file(glob.glob(os.path.join(s, "*.csv"))[0]) for s in sorted(glob.glob(os.path.join(v3_dir, "*"))) if os.path.isdir(s)]
v4_csvs = [parse_csv_file(glob.glob(os.path.join(s, "*.csv"))[0]) for s in sorted(glob.glob(os.path.join(v4_dir, "*"))) if os.path.isdir(s)]

v3_logs = [parse_profile_gpu_file(glob.glob(os.path.join(s, "*.[tT][xX][tT]"))[0]) for s in sorted(glob.glob(os.path.join(v3_dir, "*"))) if os.path.isdir(s)]
v3_logs = [l for l in v3_logs if len(l) > 1]
v4_logs = [parse_profile_gpu_file(glob.glob(os.path.join(s, "*.[tT][xX][tT]"))[0]) for s in sorted(glob.glob(os.path.join(v4_dir, "*"))) if os.path.isdir(s)]

print("=== V3 Individual Runs (ProfileGPU) ===")
for i, l in enumerate(v3_logs):
    print(f"  Run {i+1}: SLW = {l.get('SingleLayerWater', 0):.3f} ms (Draw: {l.get('SLW::Draw', 0):.3f} ms)")

print("\n=== V4 Individual Runs (ProfileGPU) ===")
for i, l in enumerate(v4_logs):
    print(f"  Run {i+1}: SLW = {l.get('SingleLayerWater', 0):.3f} ms (Draw: {l.get('SLW::Draw', 0):.3f} ms)")

print("\n=== V3 vs V4 Direct Comparison (GPU Focus) ===")
all_keys = sorted(set(list(v3_logs[0].keys()) + list(v4_logs[0].keys())))

for k in all_keys:
    if k == "_run": continue
    v3_vals = [l[k] for l in v3_logs if k in l]
    v4_vals = [l[k] for l in v4_logs if k in l]
    v3_avg = np.mean(v3_vals) if v3_vals else 0.0
    v4_avg = np.mean(v4_vals) if v4_vals else 0.0
    delta = v4_avg - v3_avg
    pct = (delta / v3_avg * 100.0) if v3_avg != 0 else 0.0
    print(f"  {k:<30}: V3 = {v3_avg:.3f} ms | V4 = {v4_avg:.3f} ms | Diff = {delta:+.3f} ms ({pct:+.2f}%)")

print("\n=== CSV Profiler Overall ===")
for k in ["GPUTime", "FrameTime_Mean", "FPS_Mean", "FPS_P5"]:
    v3_vals = [c[k] for c in v3_csvs if k in c]
    v4_vals = [c[k] for c in v4_csvs if k in c]
    v3_avg = np.mean(v3_vals) if v3_vals else 0.0
    v4_avg = np.mean(v4_vals) if v4_vals else 0.0
    delta = v4_avg - v3_avg
    pct = (delta / v3_avg * 100.0) if v3_avg != 0 else 0.0
    print(f"  {k:<30}: V3 = {v3_avg:.3f} | V4 = {v4_avg:.3f} | Diff = {delta:+.3f} ({pct:+.2f}%)")

