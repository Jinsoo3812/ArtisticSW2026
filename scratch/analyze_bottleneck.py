import os
import glob
import csv
import numpy as np

v3_dir = r"C:\Unreal Projects\ArtisticSW2026\Optimization\Data\V3_Kelvin_Capacity_and_CPU_Copy"

csvs = sorted(glob.glob(os.path.join(v3_dir, "*", "*.csv")))

all_metrics = {}

for c in csvs:
    with open(c, 'r', encoding='utf-8', errors='ignore') as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        
        for row in reader:
            if not row or len(row) < len(header): continue
            for i, h in enumerate(header):
                try:
                    val = float(row[i])
                    if h not in all_metrics:
                        all_metrics[h] = []
                    all_metrics[h].append(val)
                except ValueError:
                    pass

means = {k: np.mean(v) for k, v in all_metrics.items()}

# Print thread breakdown
print("=== V3 THREAD BREAKDOWN (CSV Profiler) ===")
threads = [
    "FrameTime",
    "GPUTime",
    "GameThreadTime",
    "RenderThreadTime",
    "RHIThreadTime",
    "Exclusive/GameThread/EventWait",
    "Exclusive/RenderThread/EventWait",
    "Exclusive/RenderThread/STAT_RDG_FlushResourcesRHI",
    "Exclusive/RenderThread/DrawSceneCommand_StartDelay",
    "DrawSceneCommand_StartDelay",
    "RHI/DrawCalls",
    "RHI/PrimitivesDrawn"
]

for t in threads:
    if t in means:
        print(f"  {t:<45}: {means[t]:.3f} ms" if "Calls" not in t and "Primitives" not in t else f"  {t:<45}: {means[t]:.1f}")

# Look for top time consumers in RenderThread / GameThread exclusive stats
print("\n=== TOP TIME CONSUMERS IN CSV PROFILER ===")
sorted_exclusive = sorted([(k, v) for k, v in means.items() if any(x in k for x in ["Exclusive/", "Time", "Delay"]) and v > 0.05 and "Mem" not in k and "Max" not in k and "Ping" not in k and "Latency" not in k], key=lambda x: x[1], reverse=True)

for k, v in sorted_exclusive[:25]:
    print(f"  {k:<55}: {v:.3f} ms")
