import csv
import glob
import os
import sys

data_dir = r"C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data"
csv_files = glob.glob(os.path.join(data_dir, "*.csv"))

if not csv_files:
    print(f"No CSV files found in {data_dir}")
    sys.exit(0)

latest_csv = max(csv_files, key=os.path.getmtime)
print(f"Analyzing Latest CSV: {os.path.basename(latest_csv)}")

with open(latest_csv, "r", encoding="utf-8-sig") as f:
    reader = csv.reader(f)
    header = next(reader)
    rows = list(reader)

print(f"Total Captured Frames: {len(rows)}")

header_map = {name.strip(): idx for idx, name in enumerate(header)}

metrics = [
    "FrameTime",
    "GPUTime",
    "RenderThreadTime",
    "RHIThreadTime",
    "GameThreadTime",
    "Exclusive/RenderThread/Water",
    "Exclusive/AllWorkers/Water",
    "Exclusive/RenderThread/RenderBasePass",
    "Exclusive/RenderThread/RenderShadows",
    "Exclusive/RenderThread/RenderLighting",
    "Exclusive/RenderThread/RenderPostProcessing",
    "Exclusive/RenderThread/RDG_Execute",
    "RHI/DrawCalls",
    "RHI/PrimitivesDrawn",
]

stats = {}
for m in metrics:
    if m in header_map:
        idx = header_map[m]
        vals = []
        for r in rows:
            if idx < len(r) and r[idx].strip():
                try:
                    vals.append(float(r[idx]))
                except ValueError:
                    pass
        if vals:
            avg_v = sum(vals) / len(vals)
            min_v = min(vals)
            max_v = max(vals)
            stats[m] = (avg_v, min_v, max_v, len(vals))

print("\n" + "="*80)
print(f"{'METRIC':<45} | {'AVG':>10} | {'MIN':>10} | {'MAX':>10}")
print("="*80)

frame_time_avg = stats.get("FrameTime", (0,))[0]
fps_avg = 1000.0 / frame_time_avg if frame_time_avg > 0 else 0.0

print(f"{'*** ESTIMATED FPS ***':<45} | {fps_avg:>10.2f} FPS | {'':>10} | {'':>10}")
print("-" * 80)

for m, (avg_v, min_v, max_v, count) in stats.items():
    unit = "ms" if "Time" in m or "Water" in m or "Pass" in m or "Render" in m or "Execute" in m else "calls"
    print(f"{m:<45} | {avg_v:>9.3f} {unit} | {min_v:>9.3f} | {max_v:>9.3f}")

print("="*80)
