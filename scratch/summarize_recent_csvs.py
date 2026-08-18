import os
import glob
import csv
import numpy as np

csv_dir = r"C:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV"
csv_files = sorted(glob.glob(os.path.join(csv_dir, "*.csv")), key=os.path.getmtime)

print(f"Total CSV files found: {len(csv_files)}")
print("-" * 105)
print(f"{'Filename':<30} | {'Frames':<7} | {'FPS Mean':<10} | {'FPS P5':<10} | {'FrameTime':<12} | {'GPUTime':<12} | {'DrawCalls':<10}")
print("-" * 105)

# analyze the last 6 files
for f in csv_files[-6:]:
    with open(f, 'r', encoding='utf-8', errors='ignore') as fp:
        reader = csv.reader(fp)
        header = [h.strip() for h in next(reader)]
        
        targets = ["FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHI/DrawCalls"]
        indices = {t: header.index(t) for t in targets if t in header}
        data = {t: [] for t in targets}
        
        for row in reader:
            if not row or len(row) <= max(indices.values()): continue
            for t, idx in indices.items():
                try:
                    data[t].append(float(row[idx]))
                except ValueError:
                    pass

    if not data["FrameTime"]: continue
    ft = np.array(data["FrameTime"])
    fps = 1000.0 / ft
    gpu = np.array(data["GPUTime"]) if data["GPUTime"] else np.array([0])
    dc = np.array(data["RHI/DrawCalls"]) if data["RHI/DrawCalls"] else np.array([0])
    
    print(f"{os.path.basename(f):<30} | {len(ft):<7} | {np.mean(fps):<10.2f} | {np.percentile(fps, 5):<10.2f} | {np.mean(ft):<6.2f} (±{np.std(ft):.2f}) | {np.mean(gpu):<6.2f} (±{np.std(gpu):.2f}) | {np.mean(dc):<10.1f}")
