import os
import glob
import csv
import numpy as np

DATA_DIR = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\Gradient_Kelvin"

targets = [
    "FrameTime", "GPUTime", "GameThreadTime", "GameThreadTime_CriticalPath",
    "DrawSceneCommand_StartDelay", "Exclusive/GameThread/EventWait",
    "RHI/DrawCalls", "RHI/PrimitivesDrawn", "GPUMem/LocalUsedMB"
]

all_data = {t: [] for t in targets}

for s in sorted(glob.glob(os.path.join(DATA_DIR, "*"))):
    if not os.path.isdir(s): continue
    csvs = glob.glob(os.path.join(s, "*.csv"))
    if not csvs: continue
    with open(csvs[0], 'r', encoding='utf-8', errors='ignore') as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        indices = {t: header.index(t) for t in targets if t in header}
        for row in reader:
            if not row or len(row) <= max(indices.values()): continue
            for t, idx in indices.items():
                try:
                    all_data[t].append(float(row[idx]))
                except ValueError:
                    pass

print("=== Gradient Kelvin vs Pure Comparison ===")
pure_vals = {
    "FPS": 72.63,
    "FPS_p5": 66.54,
    "FrameTime": 13.88,
    "GPUTime": 11.51,
    "GameThreadTime": 7.63,
    "DrawSceneCommand_StartDelay": 9.54,
    "GPUMem/LocalUsedMB": 6198.65,
    "RHI/DrawCalls": 1011.85
}

ft = np.mean(all_data["FrameTime"])
fps = np.mean(1000.0 / np.array(all_data["FrameTime"]))
fps_p5 = np.percentile(1000.0 / np.array(all_data["FrameTime"]), 5)
gpu = np.mean(all_data["GPUTime"])
gt = np.mean(all_data["GameThreadTime"]) if all_data["GameThreadTime"] else 0
draw_delay = np.mean(all_data["DrawSceneCommand_StartDelay"]) if all_data["DrawSceneCommand_StartDelay"] else 0
calls = np.mean(all_data["RHI/DrawCalls"]) if all_data["RHI/DrawCalls"] else 0

print(f"FPS: {pure_vals['FPS']:.2f} -> {fps:.2f} (Delta: +{fps - pure_vals['FPS']:.2f} FPS)")
print(f"FPS (p5): {pure_vals['FPS_p5']:.2f} -> {fps_p5:.2f} (Delta: +{fps_p5 - pure_vals['FPS_p5']:.2f} FPS)")
print(f"FrameTime: {pure_vals['FrameTime']:.2f} ms -> {ft:.2f} ms (Delta: {ft - pure_vals['FrameTime']:.2f} ms)")
print(f"GPUTime: {pure_vals['GPUTime']:.2f} ms -> {gpu:.2f} ms (Delta: {gpu - pure_vals['GPUTime']:.2f} ms)")
print(f"GameThreadTime: {pure_vals['GameThreadTime']:.2f} ms -> {gt:.2f} ms")
print(f"DrawCalls: {pure_vals['RHI/DrawCalls']:.1f} -> {calls:.1f}")
